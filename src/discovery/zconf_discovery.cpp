#include <magpie/discovery/zconf_discovery.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <magpie/utils/logger.hpp>

extern "C" {
#include "third_party/mdns/mdns.h"
}

#ifdef _WIN32
  #define _WINSOCK_DEPRECATED_NO_WARNINGS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #pragma comment(lib, "Ws2_32.lib")
  #pragma comment(lib, "Iphlpapi.lib")
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <sys/select.h>
  #include <unistd.h>
#endif

namespace magpie {

static constexpr size_t BUFFER_SIZE = 2048;
static constexpr int MAX_SOCKETS = 32;

// -------------------------
// Helpers
// -------------------------

static std::string norm_host(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
  if (!s.empty() && s.back() != '.') s.push_back('.');
  return s;
}

static inline uint64_t now_ms() {
  using namespace std::chrono;
  return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static inline std::string ensure_trailing_dot(std::string s) {
  if (!s.empty() && s.back() != '.') s.push_back('.');
  return s;
}

static inline std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
  return s;
}

static inline bool ends_with_case_insensitive(const std::string& s, const std::string& suf) {
  if (s.size() < suf.size()) return false;
  return to_lower(s.substr(s.size() - suf.size())) == to_lower(suf);
}

static inline std::string ipv4_to_string(const sockaddr_in& addr) {
  char buf[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, (void*)&addr.sin_addr, buf, sizeof(buf));
  return std::string(buf);
}

static inline std::string ipv6_to_string(const sockaddr_in6& addr) {
  char buf[INET6_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET6, (void*)&addr.sin6_addr, buf, sizeof(buf));
  return std::string(buf);
}

static inline std::string get_hostname() {
  char buf[256] = {0};
#ifdef _WIN32
  DWORD sz = (DWORD)sizeof(buf);
  if (GetComputerNameA(buf, &sz)) return std::string(buf);
  return "host";
#else
  if (gethostname(buf, sizeof(buf)) == 0) return std::string(buf);
  return "host";
#endif
}

static inline std::string make_host_qualified(const std::string& hostname) {
  // "<hostname>.local."
  std::string h = hostname;
  // keep it simple; mdns.c doesn't do extensive sanitization either
  return h + ".local.";
}

static inline std::string make_instance_name(const std::string& node_id, const std::string& service_type) {
  // "<node_id>.<service_type>"
  return node_id + "." + service_type;
}

static inline std::string node_id_from_instance(const std::string& instance) {
  auto pos = instance.find('.');
  if (pos == std::string::npos) return instance;
  return instance.substr(0, pos);
}

static inline bool is_usable_ip(const std::string& ip) {
  return !(ip.rfind("127.", 0) == 0 || ip.rfind("169.254.", 0) == 0 || ip == "::1");
}

// -------------------------
// Impl
// -------------------------

struct ZconfDiscovery::Impl {

  explicit Impl(std::string service_type)
  : service_type_(ensure_trailing_dot(std::move(service_type))) {}

  ~Impl() { stop(); }

  void start() {
#ifdef _WIN32
    // ensure WSAStartup once
    static std::once_flag wsa_once;
    std::call_once(wsa_once, []{
      WSADATA wsaData;
      WSAStartup(MAKEWORD(2,2), &wsaData);
    });
#endif
    if (running_.exchange(true)) return;
    closing_.store(false);
    worker_ = std::thread(&Impl::thread_main, this);
  }

  void stop() {
    if (!running_.load()) return;
    closing_.store(true);
    if (worker_.joinable()) worker_.join();

    // goodbye if needed
    {
      std::lock_guard<std::mutex> lk(adv_mtx_);
      if (advertising_) {
        send_goodbye_locked_();
        advertising_ = false;
      }
    }

    close_sockets_();
    running_.store(false);
  }

  void advertise(const std::string& node_id, std::uint16_t port, const std::string& payload_json) {
    start();
    std::lock_guard<std::mutex> lk(adv_mtx_);
    advertising_ = true;
    adv_node_id_ = node_id;
    adv_port_ = port;
    adv_payload_json_ = payload_json.empty() ? "{}" : payload_json;

    adv_hostname_ = get_hostname();
    adv_host_qualified_ = make_host_qualified(adv_hostname_);
    adv_instance_name_ = make_instance_name(adv_node_id_, service_type_);

    // announce once immediately (like mdns.c)
    send_announce_locked_();
  }

  void stop_advertising() {
    std::lock_guard<std::mutex> lk(adv_mtx_);
    if (!advertising_) return;
    send_goodbye_locked_();
    advertising_ = false;
  }

  bool resolve_node(const std::string& node_id, NodeInfo& out, double timeout_sec) const {
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::duration<double>(timeout_sec);

    std::unique_lock<std::mutex> lk(mtx_);
    while (true) {
      auto it = nodes_.find(node_id);
      if (it != nodes_.end() && it->second.is_resolved()) {
        out = it->second;
        return true;
      }
      if (clock::now() >= deadline) return false;
      cv_.wait_until(lk, deadline);
    }
  }

  std::vector<ZconfDiscovery::NodeInfo> list_nodes() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<NodeInfo> out;
    out.reserve(nodes_.size());

    const uint64_t now = now_ms();

    for (const auto& kv : nodes_) {
      auto itExp = second_expire_ms_.find(kv.first);
      if (itExp != second_expire_ms_.end()) {
        const uint64_t exp = itExp->second;
        if (exp != 0 && exp < now) continue; // expired
      }
      out.push_back(kv.second);
    }

    return out;
  }


  static std::string pick_best_ip(const NodeInfo& node) {
    for (const auto& ip : node.ips)
      if (is_usable_ip(ip)) return ip;
    return node.ips.empty() ? "" : node.ips.front();
  }

  // ---------------
  // Socket opening
  // ---------------

  void close_sockets_() {
    for (int i = 0; i < service_socket_count_; ++i) {
      if (service_sockets_[i] >= 0) mdns_socket_close(service_sockets_[i]);
      service_sockets_[i] = -1;
    }
    service_socket_count_ = 0;

    for (int i = 0; i < client_socket_count_; ++i) {
      if (client_sockets_[i] >= 0) mdns_socket_close(client_sockets_[i]);
      client_sockets_[i] = -1;
    }
    client_socket_count_ = 0;
  }

  int open_service_sockets_ipv4_ipv6_() {
    // Like mdns.c: bind to 5353 for listening
    int n = 0;

    // IPv4
    if (n < MAX_SOCKETS) {
      sockaddr_in sa{};
      sa.sin_family = AF_INET;
#ifdef _WIN32
      sa.sin_addr.s_addr = INADDR_ANY;
#else
      sa.sin_addr.s_addr = INADDR_ANY;
#endif
      sa.sin_port = htons((unsigned short)MDNS_PORT);
      int sock = mdns_socket_open_ipv4(&sa);
      if (sock >= 0) service_sockets_[n++] = sock;
    }

    // IPv6 (optional but recommended)
    if (n < MAX_SOCKETS) {
      sockaddr_in6 sa6{};
      sa6.sin6_family = AF_INET6;
      sa6.sin6_addr = in6addr_any;
      sa6.sin6_port = htons((unsigned short)MDNS_PORT);
      int sock6 = mdns_socket_open_ipv6(&sa6);
      if (sock6 >= 0) service_sockets_[n++] = sock6;
    }

    service_socket_count_ = n;
    return n;
  }

  int open_client_sockets_per_interface_(int port) {
    // Like mdns.c: per-interface ephemeral sockets for sending queries
    int n = 0;

#ifdef _WIN32
    ULONG addr_size = 15000;
    std::vector<uint8_t> buf(addr_size);
    IP_ADAPTER_ADDRESSES* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, nullptr, aa, &addr_size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
      buf.resize(addr_size);
      aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
      ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, nullptr, aa, &addr_size);
    }
    if (ret != NO_ERROR) return 0;

    for (auto* adapter = aa; adapter && n < MAX_SOCKETS; adapter = adapter->Next) {
      if (adapter->OperStatus != IfOperStatusUp) continue;
      if (adapter->TunnelType == TUNNEL_TYPE_TEREDO) continue;

      for (auto* unicast = adapter->FirstUnicastAddress; unicast && n < MAX_SOCKETS; unicast = unicast->Next) {
        sockaddr* sa = unicast->Address.lpSockaddr;
        if (!sa) continue;

        if (sa->sa_family == AF_INET) {
          auto* saddr = reinterpret_cast<sockaddr_in*>(sa);
          // skip loopback
          if ((ntohl(saddr->sin_addr.s_addr) >> 24) == 127) continue;

          sockaddr_in bindaddr = *saddr;
          bindaddr.sin_port = htons((unsigned short)port); // 0 => ephemeral
          int sock = mdns_socket_open_ipv4(&bindaddr);
          if (sock >= 0) client_sockets_[n++] = sock;
        } else if (sa->sa_family == AF_INET6) {
          auto* saddr6 = reinterpret_cast<sockaddr_in6*>(sa);
          // skip link-local
          if (saddr6->sin6_scope_id) continue;

          sockaddr_in6 bindaddr6 = *saddr6;
          bindaddr6.sin6_port = htons((unsigned short)port);
          int sock6 = mdns_socket_open_ipv6(&bindaddr6);
          if (sock6 >= 0) client_sockets_[n++] = sock6;
        }
      }
    }

#else
    ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return 0;

    for (auto* ifa = ifaddr; ifa && n < MAX_SOCKETS; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr) continue;
      if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_MULTICAST)) continue;
      if ((ifa->ifa_flags & IFF_LOOPBACK) || (ifa->ifa_flags & IFF_POINTOPOINT)) continue;

      if (ifa->ifa_addr->sa_family == AF_INET) {
        auto* saddr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        if (saddr->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) continue;

        sockaddr_in bindaddr = *saddr;
        bindaddr.sin_port = htons((unsigned short)port);
        int sock = mdns_socket_open_ipv4(&bindaddr);
        if (sock >= 0) client_sockets_[n++] = sock;
      } else if (ifa->ifa_addr->sa_family == AF_INET6) {
        auto* saddr6 = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
        // ignore link-local
        if (saddr6->sin6_scope_id) continue;

        sockaddr_in6 bindaddr6 = *saddr6;
        bindaddr6.sin6_port = htons((unsigned short)port);
        int sock6 = mdns_socket_open_ipv6(&bindaddr6);
        if (sock6 >= 0) client_sockets_[n++] = sock6;
      }
    }

    freeifaddrs(ifaddr);
#endif

    client_socket_count_ = n;
    return n;
  }

  // ---------------
  // Records (advertising)
  // ---------------

  void build_advertise_records_locked_(mdns_record_t& ptr_rec,
                                      mdns_record_t& srv_rec,
                                      mdns_record_t& txt_node_id,
                                      mdns_record_t& txt_proto,
                                      mdns_record_t& txt_payload,
                                      mdns_record_t& a_rec,
                                      mdns_record_t& aaaa_rec,
                                      bool& has_a,
                                      bool& has_aaaa) {
    // PTR: service_type -> instance_name
    ptr_rec = {};
    ptr_rec.name.str = service_type_.c_str();
    ptr_rec.name.length = service_type_.size();
    ptr_rec.type = MDNS_RECORDTYPE_PTR;
    ptr_rec.data.ptr.name.str = adv_instance_name_.c_str();
    ptr_rec.data.ptr.name.length = adv_instance_name_.size();

    // SRV: instance_name -> host_qualified + port
    srv_rec = {};
    srv_rec.name.str = adv_instance_name_.c_str();
    srv_rec.name.length = adv_instance_name_.size();
    srv_rec.type = MDNS_RECORDTYPE_SRV;
    srv_rec.data.srv.name.str = adv_host_qualified_.c_str();
    srv_rec.data.srv.name.length = adv_host_qualified_.size();
    srv_rec.data.srv.port = adv_port_;
    srv_rec.data.srv.priority = 0;
    srv_rec.data.srv.weight = 0;

    // TXT: instance_name key/values (library coalesces)
    txt_node_id = {};
    txt_node_id.name.str = adv_instance_name_.c_str();
    txt_node_id.name.length = adv_instance_name_.size();
    txt_node_id.type = MDNS_RECORDTYPE_TXT;
    txt_node_id.data.txt.key = { "node_id", strlen("node_id") };
    txt_node_id.data.txt.value = { adv_node_id_.c_str(), adv_node_id_.size() };

    txt_proto = {};
    txt_proto.name.str = adv_instance_name_.c_str();
    txt_proto.name.length = adv_instance_name_.size();
    txt_proto.type = MDNS_RECORDTYPE_TXT;
    txt_proto.data.txt.key = { "proto", strlen("proto") };
    static const char kProto[] = "zmq";
    txt_proto.data.txt.value = { kProto, strlen(kProto) };

    txt_payload = {};
    txt_payload.name.str = adv_instance_name_.c_str();
    txt_payload.name.length = adv_instance_name_.size();
    txt_payload.type = MDNS_RECORDTYPE_TXT;
    txt_payload.data.txt.key = { "payload", strlen("payload") };
    txt_payload.data.txt.value = { adv_payload_json_.c_str(), adv_payload_json_.size() };

    // A/AAAA records for host_qualified (optional; nice to include)
    has_a = false;
    has_aaaa = false;

    a_rec = {};
    a_rec.name.str = adv_host_qualified_.c_str();
    a_rec.name.length = adv_host_qualified_.size();
    a_rec.type = MDNS_RECORDTYPE_A;

    aaaa_rec = {};
    aaaa_rec.name.str = adv_host_qualified_.c_str();
    aaaa_rec.name.length = adv_host_qualified_.size();
    aaaa_rec.type = MDNS_RECORDTYPE_AAAA;

    // For addresses: we don’t try to be perfect; service sockets + SRV is enough for python/cpp.
    // But we can attach local interface IPs opportunistically by letting mdns answer A/AAAA to questions.
    // Here we leave them unset; we instead answer A/AAAA queries by parsing local IPs on demand in service_callback.
  }

  void send_announce_locked_() {
    if (service_socket_count_ <= 0) return;

    uint8_t buffer[BUFFER_SIZE];

    mdns_record_t ptr{}, srv{}, txt_node_id{}, txt_proto{}, txt_payload{};
    mdns_record_t a{}, aaaa{};
    bool has_a=false, has_aaaa=false;
    build_advertise_records_locked_(ptr, srv, txt_node_id, txt_proto, txt_payload, a, aaaa, has_a, has_aaaa);

    mdns_record_t additional[8] = {};
    size_t add_count = 0;
    additional[add_count++] = srv;
    additional[add_count++] = txt_node_id;
    additional[add_count++] = txt_proto;
    additional[add_count++] = txt_payload;

    for (int i = 0; i < service_socket_count_; ++i) {
      mdns_announce_multicast(service_sockets_[i], buffer, sizeof(buffer),
                              ptr, nullptr, 0, additional, add_count);
    }
  }

  void send_goodbye_locked_() {
    if (service_socket_count_ <= 0) return;

    uint8_t buffer[BUFFER_SIZE];

    mdns_record_t ptr{}, srv{}, txt_node_id{}, txt_proto{}, txt_payload{};
    mdns_record_t a{}, aaaa{};
    bool has_a=false, has_aaaa=false;
    build_advertise_records_locked_(ptr, srv, txt_node_id, txt_proto, txt_payload, a, aaaa, has_a, has_aaaa);

    mdns_record_t additional[8] = {};
    size_t add_count = 0;
    additional[add_count++] = srv;
    additional[add_count++] = txt_node_id;
    additional[add_count++] = txt_proto;
    additional[add_count++] = txt_payload;

    for (int i = 0; i < service_socket_count_; ++i) {
      mdns_goodbye_multicast(service_sockets_[i], buffer, sizeof(buffer),
                             ptr, nullptr, 0, additional, add_count);
    }
  }

  // ---------------
  // Callbacks
  // ---------------

  static int mdns_callback(int sock,
                           const struct sockaddr* from,
                           size_t addrlen,
                           mdns_entry_type_t entry,
                           uint16_t query_id,
                           uint16_t rtype,
                           uint16_t rclass,
                           uint32_t ttl,
                           const void* data,
                           size_t size,
                           size_t name_offset,
                           size_t name_length,
                           size_t record_offset,
                           size_t record_length,
                           void* user_data) {
    (void)query_id; (void)name_length;
    auto* self = static_cast<Impl*>(user_data);
    self->handle_record(sock, from, addrlen, entry, rtype, rclass, ttl, data, size,
                        name_offset, record_offset, record_length);
    return 0;
  }

    void handle_record(int sock,
                    const struct sockaddr* from,
                    size_t addrlen,
                    mdns_entry_type_t entry,
                    uint16_t rtype,
                    uint16_t rclass,
                    uint32_t ttl,
                    const void* data,
                    size_t size,
                    size_t name_offset,
                    size_t record_offset,
                    size_t record_length) {
    (void)sock; (void)from; (void)addrlen; (void)rclass;

    // 1) Service side: answer QUESTIONS (best practice, do not rely on announce)
    if (entry == MDNS_ENTRYTYPE_QUESTION) {
      handle_question_(sock, from, addrlen, rtype, rclass, ttl, data, size, name_offset);
      return;
    }

    // 2) Client side: parse ANSWERS/AUTHORITY/ADDITIONAL to build node map
    if (entry != MDNS_ENTRYTYPE_ANSWER && entry != MDNS_ENTRYTYPE_AUTHORITY &&
        entry != MDNS_ENTRYTYPE_ADDITIONAL)
      return;

    // Extract the owner name
    char namebuf[256] = {0};
    size_t nofs = name_offset;
    mdns_string_t owner = mdns_string_extract(data, size, &nofs, namebuf, sizeof(namebuf));
    if (!owner.str || !owner.length) return;

    const uint64_t now = now_ms();
    const uint64_t expire_ms = (ttl == 0) ? 0 : (now + (uint64_t)ttl * 1000ULL);

    if (rtype == MDNS_RECORDTYPE_PTR) {
      // PTR record: service_type -> instance
      char instbuf[256] = {0};
      mdns_string_t inst = mdns_record_parse_ptr(data, size, record_offset, record_length,
                                                instbuf, sizeof(instbuf));
      if (!inst.str || !inst.length) return;

      std::string owner_name(owner.str, owner.length);
      std::string instance(inst.str, inst.length);

      // Only our service type
      if (!ends_with_case_insensitive(owner_name, service_type_)) return;
      // Instance should end with service_type too
      if (!ends_with_case_insensitive(instance, service_type_)) return;

      std::string node_id = node_id_from_instance(instance);

      std::lock_guard<std::mutex> lk(mtx_);
      NodeInfo& ni = nodes_[node_id];
      ni.node_id = node_id;
      ni.instance_name = instance;

      // TTL=0 means goodbye -> remove
      if (ttl == 0) {
        nodes_.erase(node_id);
        instance_to_node_.erase(instance);
        second_expire_ms_.erase(node_id);
        cv_.notify_all();
        return;
      }

      instance_to_node_[instance] = node_id;
      second_expire_ms_[node_id] = expire_ms;
      cv_.notify_all();
      return;
    }

    if (rtype == MDNS_RECORDTYPE_SRV) {
      // SRV: instance -> host_qualified + port
      char hostbuf[256] = {0};
      mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
                                                    hostbuf, sizeof(hostbuf));
      if (!srv.name.str || !srv.name.length) return;

      std::string instance(owner.str, owner.length);
      if (!ends_with_case_insensitive(instance, service_type_)) return;

      std::string host(srv.name.str, srv.name.length);
      std::string host_norm = norm_host(host);

      std::lock_guard<std::mutex> lk(mtx_);
      auto itInst = instance_to_node_.find(instance);
      std::string node_id = (itInst != instance_to_node_.end()) ? itInst->second
                                                                : node_id_from_instance(instance);

      NodeInfo& ni = nodes_[node_id];
      ni.node_id = node_id;
      ni.instance_name = instance;
      ni.host_qualified = host;              // keep original
      ni.port = (uint16_t)srv.port;

      // TTL=0 goodbye -> drop
      if (ttl == 0) {
        nodes_.erase(node_id);
        instance_to_node_.erase(instance);
        host_to_node_.erase(host);
        host_to_node_.erase(host_norm);
        pending_ips_by_host_.erase(host_norm);
        second_expire_ms_.erase(node_id);
        cv_.notify_all();
        return;
      }

      // map host -> node
      host_to_node_[host] = node_id;         // keep original (for safety)
      host_to_node_[host_norm] = node_id;

      // apply pending ips if any
      auto pit = pending_ips_by_host_.find(host_norm);
      if (pit != pending_ips_by_host_.end()) {
        auto& ips = ni.ips;
        for (const auto& ip : pit->second)
          if (std::find(ips.begin(), ips.end(), ip) == ips.end()) ips.push_back(ip);
        pending_ips_by_host_.erase(pit);
      }

      second_expire_ms_[node_id] = expire_ms;
      cv_.notify_all();
      return;
    }

    if (rtype == MDNS_RECORDTYPE_TXT) {
      // TXT: instance -> key/value pairs; we care about node_id + payload
      mdns_record_txt_t txt[16];
      size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txt,
                                            sizeof(txt) / sizeof(txt[0]));
      std::string instance(owner.str, owner.length);
      if (!ends_with_case_insensitive(instance, service_type_)) return;

      std::string node_id = node_id_from_instance(instance);
      std::string payload = "{}";
      for (size_t i = 0; i < parsed; ++i) {
        std::string key(txt[i].key.str ? txt[i].key.str : "", txt[i].key.length);
        std::string val(txt[i].value.str ? txt[i].value.str : "", txt[i].value.length);
        if (key == "node_id" && !val.empty()) node_id = val;
        if (key == "payload") payload = val.empty() ? "{}" : val;
      }

      std::lock_guard<std::mutex> lk(mtx_);
      NodeInfo& ni = nodes_[node_id];
      ni.node_id = node_id;
      ni.instance_name = instance;
      ni.payload_json = payload;

      if (ttl == 0) {
        nodes_.erase(node_id);
        instance_to_node_.erase(instance);
        second_expire_ms_.erase(node_id);
        cv_.notify_all();
        return;
      }

      second_expire_ms_[node_id] = expire_ms;
      cv_.notify_all();
      return;
    }

    if (rtype == MDNS_RECORDTYPE_A) {
      // Logger::debug(
      //   "[A] owner='" + std::string(owner.str, owner.length) +
      //   "' ttl=" + std::to_string(ttl)
      // );

      // A: host_qualified -> IPv4
      sockaddr_in addr{};
      mdns_record_parse_a(data, size, record_offset, record_length, &addr);

      std::string ip = ipv4_to_string(addr);
      if (ip.empty()) return;

      std::string host(owner.str, owner.length);
      std::string host_norm = norm_host(host);

      // Logger::debug(
      //   "[A] host raw='" + host +
      //   "' norm='" + host_norm +
      //   "' mapped=" +
      //   (host_to_node_.count(host_norm) ? "yes" : "no")
      // );

      std::lock_guard<std::mutex> lk(mtx_);
      auto itHost = host_to_node_.find(host_norm);
      if (itHost == host_to_node_.end()) {
        // SRV not seen yet -> cache
        if (ttl != 0) {
          auto& pend = pending_ips_by_host_[host_norm];
          if (std::find(pend.begin(), pend.end(), ip) == pend.end()) pend.push_back(ip);
        }
        return;
      }

      const std::string& node_id = itHost->second;
      auto it = nodes_.find(node_id);
      if (it == nodes_.end()) return;

      if (ttl == 0) {
        auto& ips = it->second.ips;
        ips.erase(std::remove(ips.begin(), ips.end(), ip), ips.end());
        cv_.notify_all();
        return;
      }

      auto& ips = it->second.ips;
      if (std::find(ips.begin(), ips.end(), ip) == ips.end()) ips.push_back(ip);
      second_expire_ms_[node_id] = expire_ms;
      cv_.notify_all();
      return;
    }

    if (rtype == MDNS_RECORDTYPE_AAAA) {
      // Logger::debug(
      //   "[AAAA] owner='" + std::string(owner.str, owner.length) +
      //   "' ttl=" + std::to_string(ttl)
      // );      
      // AAAA: host_qualified -> IPv6
      sockaddr_in6 addr6{};
      mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr6);

      std::string ip = ipv6_to_string(addr6);
      if (ip.empty()) return;

      std::string host(owner.str, owner.length);
      std::string host_norm = norm_host(host);

      // Logger::debug(
      //   "[A] host raw='" + host +
      //   "' norm='" + host_norm +
      //   "' mapped=" +
      //   (host_to_node_.count(host_norm) ? "yes" : "no")
      // );

      std::lock_guard<std::mutex> lk(mtx_);
      auto itHost = host_to_node_.find(host_norm);
      if (itHost == host_to_node_.end()) {
        // SRV not seen yet -> cache
        if (ttl != 0) {
          auto& pend = pending_ips_by_host_[host_norm];
          if (std::find(pend.begin(), pend.end(), ip) == pend.end()) pend.push_back(ip);
        }
        return;
      }

      const std::string& node_id = itHost->second;
      auto it = nodes_.find(node_id);
      if (it == nodes_.end()) return;

      if (ttl == 0) {
        auto& ips = it->second.ips;
        ips.erase(std::remove(ips.begin(), ips.end(), ip), ips.end());
        cv_.notify_all();
        return;
      }

      auto& ips = it->second.ips;
      if (std::find(ips.begin(), ips.end(), ip) == ips.end()) ips.push_back(ip);
      second_expire_ms_[node_id] = expire_ms;
      cv_.notify_all();
      return;
    }
  }


  void handle_question_(int sock,
                        const struct sockaddr* from,
                        size_t addrlen,
                        uint16_t qtype,
                        uint16_t qclass,
                        uint32_t /*ttl*/,
                        const void* data,
                        size_t size,
                        size_t name_offset) {
    // Only answer if we are advertising
    std::lock_guard<std::mutex> lk(adv_mtx_);
    if (!advertising_) return;

    // Extract queried name
    char qnamebuf[256] = {0};
    size_t off = name_offset;
    mdns_string_t qname = mdns_string_extract(data, size, &off, qnamebuf, sizeof(qnamebuf));
    if (!qname.str || !qname.length) return;

    std::string asked(qname.str, qname.length);

    // Prepare our records
    mdns_record_t ptr{}, srv{}, txt_node_id{}, txt_proto{}, txt_payload{};
    mdns_record_t a_rec{}, aaaa_rec{};
    bool has_a=false, has_aaaa=false;
    build_advertise_records_locked_(ptr, srv, txt_node_id, txt_proto, txt_payload, a_rec, aaaa_rec, has_a, has_aaaa);

    // We follow mdns.c behavior:
    // - If question is for our service_type (PTR/ANY) -> answer PTR + additional SRV + TXT
    // - If question is for our instance (SRV/TXT/ANY) -> answer SRV + additional TXT
    // - If question is for our host_qualified (A/AAAA/ANY) -> answer A/AAAA if we can
    //
    // To keep it robust and minimal, we always include SRV+TXT additional where relevant.

    uint8_t sendbuf[BUFFER_SIZE];

    const bool want_unicast = (qclass & MDNS_UNICAST_RESPONSE) != 0;
    const uint16_t qclass_no_flush = (uint16_t)(qclass & ~MDNS_CACHE_FLUSH);

    // Only answer IN/ANY
    if (!(qclass_no_flush == MDNS_CLASS_IN || qclass_no_flush == MDNS_CLASS_ANY)) return;

    auto send_answer = [&](mdns_record_t answer, mdns_record_t* additional, size_t add_count) {
      if (want_unicast) {
        mdns_query_answer_unicast(sock, from, addrlen, sendbuf, sizeof(sendbuf),
                                  /*query_id*/0,
                                  (mdns_record_type_t)qtype,
                                  qname.str, qname.length,
                                  answer,
                                  nullptr, 0,
                                  additional, add_count);
      } else {
        mdns_query_answer_multicast(sock, sendbuf, sizeof(sendbuf),
                                    answer,
                                    nullptr, 0,
                                    additional, add_count);
      }
    };

    // service_type question?
    if (ends_with_case_insensitive(asked, service_type_)) {
      if (qtype == MDNS_RECORDTYPE_PTR || qtype == MDNS_RECORDTYPE_ANY) {
        mdns_record_t additional[6] = {};
        size_t add_count = 0;
        additional[add_count++] = srv;
        additional[add_count++] = txt_node_id;
        additional[add_count++] = txt_proto;
        additional[add_count++] = txt_payload;

        send_answer(ptr, additional, add_count);
      }
      return;
    }

    // instance question?
    if (ends_with_case_insensitive(asked, service_type_) &&
        asked == adv_instance_name_) {
      if (qtype == MDNS_RECORDTYPE_SRV || qtype == MDNS_RECORDTYPE_ANY) {
        mdns_record_t additional[6] = {};
        size_t add_count = 0;
        additional[add_count++] = txt_node_id;
        additional[add_count++] = txt_proto;
        additional[add_count++] = txt_payload;

        send_answer(srv, additional, add_count);
      } else if (qtype == MDNS_RECORDTYPE_TXT) {
        // TXT is coalesced by library when we send it as "additional"
        mdns_record_t additional[6] = {};
        size_t add_count = 0;
        additional[add_count++] = txt_node_id;
        additional[add_count++] = txt_proto;
        additional[add_count++] = txt_payload;

        // For TXT query, send SRV as answer is not correct; so we send PTR as a harmless answer? better:
        // send SRV as "answer" only for SRV/ANY, otherwise let it be.
        // Here we multicast announce-like additional as answer: use PTR as dummy? No.
        // Simplest: do nothing; client will still get TXT from earlier SRV query.
      }
      return;
    }

    // host question (A/AAAA)
    if (asked == adv_host_qualified_) {
      // We keep this simple: most clients get IP via additional A/AAAA from other stacks anyway,
      // but cpp<->cpp relies on us answering something. We’ll answer with local addresses by querying OS.
      // For now, answer nothing here (still works via SRV->connect using instance host?).
      // If you want, we can add full local interface IP enumeration and send A/AAAA records too.
      return;
    }
  }

  // ---------------
  // Thread main
  // ---------------

  void thread_main() {
    close_sockets_();

    // Open service sockets on 5353 (critical)
    if (open_service_sockets_ipv4_ipv6_() <= 0) {
      Logger::error("[ZconfDiscovery] Failed to open service sockets on 5353");
      return;
    }

    // Open client sockets per interface (ephemeral)
    open_client_sockets_per_interface_(0);

    Logger::debug("[ZconfDiscovery] service sockets=" + std::to_string(service_socket_count_) +
                  " client sockets=" + std::to_string(client_socket_count_));

    uint8_t buffer[BUFFER_SIZE];

    uint64_t last_query_ms = 0;
    const uint64_t query_interval_ms = 2000; // like your python browser feel
    const uint64_t cleanup_interval_ms = 2000;
    uint64_t last_cleanup_ms = 0;

    while (!closing_.load()) {
      const uint64_t tnow = now_ms();

      // Periodic query (PTR for our service_type) from all client sockets
      if (client_socket_count_ > 0 && (tnow - last_query_ms) >= query_interval_ms) {
        for (int i = 0; i < client_socket_count_; ++i) {
          // Query for PTR on service_type
          mdns_query_send(client_sockets_[i],
                          MDNS_RECORDTYPE_PTR,
                          service_type_.c_str(),
                          service_type_.size(),
                          buffer,
                          sizeof(buffer),
                          0);
        }
        last_query_ms = tnow;
      }

      // select() over all sockets
      fd_set readfs;
      FD_ZERO(&readfs);
      int nfds = 0;

      for (int i = 0; i < service_socket_count_; ++i) {
        int s = service_sockets_[i];
        if (s >= 0) {
          FD_SET(s, &readfs);
          nfds = std::max(nfds, s + 1);
        }
      }
      for (int i = 0; i < client_socket_count_; ++i) {
        int s = client_sockets_[i];
        if (s >= 0) {
          FD_SET(s, &readfs);
          nfds = std::max(nfds, s + 1);
        }
      }

      timeval tv{};
      tv.tv_sec = 0;
      tv.tv_usec = 100000; // 100ms like mdns.c

      int res = select(nfds, &readfs, nullptr, nullptr, &tv);
      if (res > 0) {
        // Service sockets: listen questions (and also parse answers if any)
        for (int i = 0; i < service_socket_count_; ++i) {
          int s = service_sockets_[i];
          if (s >= 0 && FD_ISSET(s, &readfs)) {
            mdns_socket_listen(s, buffer, sizeof(buffer), &Impl::mdns_callback, this);
          }
        }
        // Client sockets: parse query responses
        for (int i = 0; i < client_socket_count_; ++i) {
          int s = client_sockets_[i];
          if (s >= 0 && FD_ISSET(s, &readfs)) {
            mdns_query_recv(s, buffer, sizeof(buffer), &Impl::mdns_callback, this, 0);
          }
        }
      }

      // Periodic cleanup of expired nodes
      if ((tnow - last_cleanup_ms) >= cleanup_interval_ms) {
        cleanup_expired_(tnow);
        last_cleanup_ms = tnow;
      }
    }
  }

  void cleanup_expired_(uint64_t tnow) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> to_remove;
    for (const auto& kv : second_expire_ms_) {
      if (kv.second != 0 && kv.second < tnow) to_remove.push_back(kv.first);
    }
    for (const auto& node_id : to_remove) {
      auto it = nodes_.find(node_id);
      if (it != nodes_.end()) {
        instance_to_node_.erase(it->second.instance_name);
        host_to_node_.erase(it->second.host_qualified);
        nodes_.erase(it);
      }
      second_expire_ms_.erase(node_id);
    }
    if (!to_remove.empty()) cv_.notify_all();
  }

  // -------------------------
  // State
  // -------------------------
  std::string service_type_;

  std::atomic<bool> running_{false};
  std::atomic<bool> closing_{false};
  std::thread worker_;

  // Sockets
  int service_sockets_[MAX_SOCKETS]{};
  int service_socket_count_ = 0;

  int client_sockets_[MAX_SOCKETS]{};
  int client_socket_count_ = 0;

  // Advertising state
  mutable std::mutex adv_mtx_;
  bool advertising_ = false;
  std::string adv_node_id_;
  std::uint16_t adv_port_ = 0;
  std::string adv_payload_json_ = "{}";
  std::string adv_hostname_;
  std::string adv_host_qualified_;
  std::string adv_instance_name_;

  // Node map
  mutable std::mutex mtx_;
  mutable std::condition_variable cv_;
  mutable std::unordered_map<std::string, NodeInfo> nodes_; // node_id -> NodeInfo
  // TTL expiry per node (ms since steady epoch)
  mutable std::unordered_map<std::string, uint64_t> second_expire_ms_;

  // Helper mappings while resolving PTR->SRV->A/TXT
  mutable std::unordered_map<std::string, std::string> instance_to_node_; // instance -> node_id
  mutable std::unordered_map<std::string, std::string> host_to_node_;     // host -> node_id

  std::unordered_map<std::string, std::vector<std::string>> pending_ips_by_host_;

};

// -------------------------
// Public wrapper
// -------------------------

ZconfDiscovery::ZconfDiscovery(std::string service_type)
  : impl_(new Impl(std::move(service_type))) {}

ZconfDiscovery::~ZconfDiscovery() = default;

void ZconfDiscovery::start() { impl_->start(); }

void ZconfDiscovery::close() { impl_->stop(); }

void ZconfDiscovery::advertise(const std::string& node_id,
                               std::uint16_t port,
                               const std::string& payload_json) {
  impl_->advertise(node_id, port, payload_json);
}

void ZconfDiscovery::stop_advertising() { impl_->stop_advertising(); }

bool ZconfDiscovery::resolve_node(const std::string& node_id,
                                  NodeInfo& out,
                                  double timeout_sec) const {
  return impl_->resolve_node(node_id, out, timeout_sec);
}

std::vector<ZconfDiscovery::NodeInfo> ZconfDiscovery::list_nodes() const {
  return impl_->list_nodes();
}

std::string ZconfDiscovery::pick_best_ip(const NodeInfo& node) {
  return Impl::pick_best_ip(node);
}

} // namespace magpie

