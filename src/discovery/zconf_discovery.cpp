#include <magpie/discovery/zconf_discovery.hpp>

#include <magpie/utils/logger.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif

extern "C" {
#include "third_party/mdns/mdns.h"
}

namespace magpie {

static constexpr size_t BUF_CAP = 2048;
static constexpr int MAX_SOCKETS = 32;

static uint64_t now_ms() {
  using namespace std::chrono;
  return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static std::string to_lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

static std::string ensure_trailing_dot(std::string s) {
  if (s.empty()) return s;
  if (s.back() != '.') s.push_back('.');
  return s;
}

static bool ends_with_case_insensitive(const std::string& s, const std::string& suffix) {
  if (suffix.size() > s.size()) return false;
  std::string a = to_lower(s.substr(s.size() - suffix.size()));
  std::string b = to_lower(suffix);
  return a == b;
}

static std::string norm_host(const std::string& h) {
  // normalize: lowercase + ensure trailing dot
  return ensure_trailing_dot(to_lower(h));
}

static std::string ipv4_to_string(const sockaddr_in& a) {
  char ip[INET_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET, (void*)&a.sin_addr, ip, sizeof(ip))) return {};
  return std::string(ip);
}

static std::string ipv6_to_string(const sockaddr_in6& a) {
  char ip[INET6_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET6, (void*)&a.sin6_addr, ip, sizeof(ip))) return {};
  return std::string(ip);
}

#ifndef _WIN32
static void get_hostname(std::string& out) {
  char buf[256] = {0};
  if (gethostname(buf, sizeof(buf) - 1) == 0) out = buf;
  else out = "magpie";
}
#else
static void get_hostname(std::string& out) {
  char buf[256] = {0};
  DWORD sz = (DWORD)sizeof(buf);
  if (GetComputerNameA(buf, &sz)) out = buf;
  else out = "magpie";
}
#endif

// Enumerate local addresses + open sockets per interface (client mode).
// This follows mdns.c logic: "when sending, each socket can only send to one interface".
struct IfaceAddr {
  bool is_v6 = false;
  sockaddr_in v4{};
  sockaddr_in6 v6{};
};

#ifndef _WIN32

static bool is_good_iface(const ifaddrs* ifa) {
  if (!ifa || !ifa->ifa_addr) return false;
  if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_MULTICAST)) return false;
  if ((ifa->ifa_flags & IFF_POINTOPOINT)) return false;
  // NOTE: mdns.c skips LOOPBACK for opening client sockets; we keep it skipped for sending,
  // but we still allow discovered IP lists to include 127.* if advertised by others.
  if (ifa->ifa_flags & IFF_LOOPBACK) return false;
  return true;
}

static std::vector<IfaceAddr> enumerate_ifaces() {
  std::vector<IfaceAddr> out;
  ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) < 0) return out;

  for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    if (!is_good_iface(ifa)) continue;

    if (ifa->ifa_addr->sa_family == AF_INET) {
      auto* saddr = (sockaddr_in*)ifa->ifa_addr;
      if (saddr->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) continue;
      IfaceAddr ia;
      ia.is_v6 = false;
      ia.v4 = *saddr;
      out.push_back(ia);
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {
      auto* saddr6 = (sockaddr_in6*)ifa->ifa_addr;
      // ignore link-local (scope_id != 0 in mdns.c example)
      if (saddr6->sin6_scope_id) continue;

      // ignore ::1 and mapped 127.0.0.1 variants like mdns.c
      static const unsigned char localhost[] =
          {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
      static const unsigned char localhost_mapped[] =
          {0,0,0,0,0,0,0,0,0,0,0xff,0xff,0x7f,0,0,1};
      if (!memcmp(saddr6->sin6_addr.s6_addr, localhost, 16)) continue;
      if (!memcmp(saddr6->sin6_addr.s6_addr, localhost_mapped, 16)) continue;

      IfaceAddr ia;
      ia.is_v6 = true;
      ia.v6 = *saddr6;
      out.push_back(ia);
    }
  }

  freeifaddrs(ifaddr);
  return out;
}

static bool is_bad_virtual_iface_name(const char* name) {
  if (!name) return false;
  // common virtual/container bridges & peers
  if (strcmp(name, "docker0") == 0) return true;
  if (strncmp(name, "br-",   3) == 0) return true;
  if (strncmp(name, "veth",  4) == 0) return true;
  if (strncmp(name, "virbr", 5) == 0) return true;
  if (strncmp(name, "tun",   3) == 0) return true;
  if (strncmp(name, "wg",    2) == 0) return true;
  return false;
}

static int open_client_sockets(int* socks, int max_socks, int port) {
  auto ifs = enumerate_ifaces();
  int n = 0;
  for (auto& ia : ifs) {
    if (n >= max_socks) break;
    if (!ia.is_v6) {
      ia.v4.sin_port = htons((unsigned short)port);
      int s = mdns_socket_open_ipv4(&ia.v4);
      if (s >= 0) socks[n++] = s;
    } else {
      ia.v6.sin6_port = htons((unsigned short)port);
      int s = mdns_socket_open_ipv6(&ia.v6);
      if (s >= 0) socks[n++] = s;
    }
  }
  return n;
}

static int open_service_sockets(int* socks, int max_socks) {
  // Same idea as mdns.c: one socket per family, bound to MDNS_PORT on INADDR_ANY.
  int n = 0;

  if (n < max_socks) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons((unsigned short)MDNS_PORT);
    int s = mdns_socket_open_ipv4(&sa);
    if (s >= 0) socks[n++] = s;
  }

  if (n < max_socks) {
    sockaddr_in6 sa6{};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = htons((unsigned short)MDNS_PORT);
    int s = mdns_socket_open_ipv6(&sa6);
    if (s >= 0) socks[n++] = s;
  }

  return n;
}

#else
// Windows: keep a minimal single-socket fallback.
// If you need full parity like mdns.c on Windows, we can port open_client_sockets using GetAdaptersAddresses.
static int open_client_sockets(int* socks, int max_socks, int port) {
  (void)port;
  int n = 0;
  if (n < max_socks) {
    int s = mdns_socket_open_ipv4(nullptr);
    if (s >= 0) socks[n++] = s;
  }
  if (n < max_socks) {
    int s = mdns_socket_open_ipv6(nullptr);
    if (s >= 0) socks[n++] = s;
  }
  return n;
}

static int open_service_sockets(int* socks, int max_socks) {
  int n = 0;
  if (n < max_socks) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons((unsigned short)MDNS_PORT);
    int s = mdns_socket_open_ipv4(&sa);
    if (s >= 0) socks[n++] = s;
  }
  if (n < max_socks) {
    sockaddr_in6 sa6{};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = htons((unsigned short)MDNS_PORT);
    int s = mdns_socket_open_ipv6(&sa6);
    if (s >= 0) socks[n++] = s;
  }
  return n;
}
#endif

struct ZconfDiscovery::Impl {
  explicit Impl(std::string service_type)
      : service_type_(ensure_trailing_dot(std::move(service_type))) {}

  // ---- Public facing state ----
  std::string service_type_;

  mutable std::mutex mtx_;
  mutable std::condition_variable cv_;
  std::unordered_map<std::string, NodeInfo> nodes_;                  // node_id -> NodeInfo
  std::unordered_map<std::string, std::string> instance_to_node_;    // instance -> node_id
  std::unordered_map<std::string, std::string> host_to_node_;        // host(norm) -> node_id

  // If A/AAAA arrives before SRV host mapping, we keep it here and apply later.
  std::unordered_map<std::string, std::vector<std::string>> pending_ips_by_host_;  // host(norm)->ips

  // TTL handling (simple): node_id -> expire time; list_nodes can filter.
  std::unordered_map<std::string, uint64_t> expire_ms_by_node_;

  // ---- Advertising state ----
  mutable std::mutex adv_mtx_;
  bool advertising_ = false;
  std::string adv_node_id_;
  std::uint16_t adv_port_ = 0;
  std::string adv_payload_json_ = "{}";
  std::vector<std::string> adv_ips_;
  std::string adv_hostname_;
  std::string adv_instance_;
  std::string adv_host_qualified_;

  // ---- Thread ----
  std::atomic<bool> running_{false};
  std::atomic<bool> closing_{false};
  std::thread worker_;

  // sockets
  int service_socks_[MAX_SOCKETS]{};
  int client_socks_[MAX_SOCKETS]{};
  int n_service_ = 0;
  int n_client_ = 0;

  // Buffers (thread-local usage)
  uint8_t recvbuf_[BUF_CAP]{};
  char sendbuf_[BUF_CAP]{};

  // Query pacing
  uint64_t last_ptr_query_ms_ = 0;
  uint64_t last_refresh_ms_ = 0;

  // Avoid spamming identical follow-up queries
  std::unordered_set<std::string> queried_instance_;  // instance we already queried SRV/TXT for
  std::unordered_set<std::string> queried_host_;      // host we already queried A/AAAA for

  // ---- API ----
  void start() {
    if (running_) return;
    closing_ = false;
    running_ = true;
    worker_ = std::thread(&Impl::thread_main, this);
  }

  void close() {
    if (!running_) return;
    closing_ = true;
    if (worker_.joinable()) worker_.join();

    // Goodbye if still advertising
    send_goodbye_if_needed_();

    for (int i = 0; i < n_service_; ++i) mdns_socket_close(service_socks_[i]);
    for (int i = 0; i < n_client_;  ++i) mdns_socket_close(client_socks_[i]);
    n_service_ = n_client_ = 0;

    running_ = false;
  }

  void advertise(const std::string& node_id,
                 std::uint16_t port,
                 const std::string& payload_json,
                 const std::vector<std::string>& ips) {
    start();

    std::lock_guard<std::mutex> lk(adv_mtx_);
    advertising_ = true;
    adv_node_id_ = node_id;
    adv_port_ = port;
    adv_payload_json_ = payload_json.empty() ? "{}" : payload_json;

    // Build names like mdns.c does: instance and host qualified.
    // We use node_id as hostname-ish identifier.
    adv_instance_ = ensure_trailing_dot(node_id + "." + service_type_);
    adv_host_qualified_ = ensure_trailing_dot(node_id + ".local.");

    // Choose IPs
    adv_ips_.clear();
    if (!ips.empty()) {
      adv_ips_ = ips;
    } else {
      // enumerate local interface IPs (both v4 and v6)
      // We include everything found (like python does), even docker/veth.
      // If you want filtering, do it at caller or later.
#ifndef _WIN32
      ifaddrs* ifaddr = nullptr;
      if (getifaddrs(&ifaddr) == 0) {
        for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
          if (!ifa->ifa_addr) continue;
          if (!(ifa->ifa_flags & IFF_UP)) continue;

          // NEW: skip docker/bridge/veth/etc from advertisement
          if (is_bad_virtual_iface_name(ifa->ifa_name)) continue;
                    
          if (ifa->ifa_addr->sa_family == AF_INET) {
            auto* sa = (sockaddr_in*)ifa->ifa_addr;
            char ip[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) {
              adv_ips_.push_back(ip);
            }
          } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            auto* sa6 = (sockaddr_in6*)ifa->ifa_addr;
            char ip[INET6_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET6, &sa6->sin6_addr, ip, sizeof(ip))) {
              adv_ips_.push_back(ip);
            }
          }
        }
        freeifaddrs(ifaddr);
      }
#else
      // Windows: keep simple, caller can supply ips.
#endif
      // de-dup
      std::sort(adv_ips_.begin(), adv_ips_.end());
      adv_ips_.erase(std::unique(adv_ips_.begin(), adv_ips_.end()), adv_ips_.end());
    }

    // Also send an announce immediately (best practice)
    send_announce_locked_();
  }

  void stop_advertising() {
    std::lock_guard<std::mutex> lk(adv_mtx_);
    if (!advertising_) return;
    // Send goodbye before flipping off
    send_goodbye_locked_();
    advertising_ = false;
  }

  std::vector<NodeInfo> list_nodes() const {
    const uint64_t now = now_ms();
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<NodeInfo> out;
    out.reserve(nodes_.size());
    for (auto const& kv : nodes_) {
      auto itexp = expire_ms_by_node_.find(kv.first);
      if (itexp != expire_ms_by_node_.end() && itexp->second != 0 && itexp->second < now) {
        continue;  // expired
      }
      out.push_back(kv.second);
    }
    return out;
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

  // ---- mdns glue ----
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
    self->handle_record(sock, from, addrlen, entry, rtype, rclass, ttl,
                        data, size, name_offset, record_offset, record_length);
    return 0;
  }

  // Convert "<node_id>.<service_type>" -> node_id
  std::string node_id_from_instance(const std::string& instance) const {
    // instance is "<node_id>.<service_type_>" typically with trailing dot
    std::string s = instance;
    s = to_lower(s);
    std::string st = to_lower(service_type_);
    // Ensure both have trailing dot
    s = ensure_trailing_dot(s);
    st = ensure_trailing_dot(st);

    if (!ends_with_case_insensitive(s, st)) return {};
    // strip ".<service_type_>"
    // Example: nodeid._magpie._tcp.local.
    // Remove suffix length
    std::string node = s.substr(0, s.size() - st.size());
    // node might end with '.', remove it
    if (!node.empty() && node.back() == '.') node.pop_back();
    return node;
  }

  // ---- service side: answer QUESTIONS (like mdns.c) ----
  void handle_question_(int sock,
                        const struct sockaddr* from,
                        size_t addrlen,
                        uint16_t rtype,
                        uint16_t rclass,
                        uint32_t /*ttl*/,
                        const void* data,
                        size_t size,
                        size_t name_offset) {
    std::lock_guard<std::mutex> lk(adv_mtx_);
    if (!advertising_) return;

    char namebuf[256]{};
    size_t ofs = name_offset;
    mdns_string_t qname = mdns_string_extract(data, size, &ofs, namebuf, sizeof(namebuf));
    if (!qname.str || !qname.length) return;

    const std::string qn(qname.str, qname.length);
    const uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);

    // We answer for:
    // - PTR service_type_ -> instance
    // - SRV instance -> host_qualified + port
    // - TXT instance -> node_id, payload
    // - A/AAAA host_qualified -> IP(s)

    // Build records
    mdns_record_t ptr{};
    ptr.name = mdns_string_t{ service_type_.c_str(), service_type_.size() };
    ptr.type = MDNS_RECORDTYPE_PTR;
    ptr.data.ptr.name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };

    mdns_record_t srv{};
    srv.name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };
    srv.type = MDNS_RECORDTYPE_SRV;
    srv.data.srv.name = mdns_string_t{ adv_host_qualified_.c_str(), adv_host_qualified_.size() };
    srv.data.srv.port = adv_port_;
    srv.data.srv.priority = 0;
    srv.data.srv.weight = 0;

    // TXT (two keys like mdns.c style; library coalesces)
    mdns_record_t txt_rec[2]{};
    txt_rec[0].name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };
    txt_rec[0].type = MDNS_RECORDTYPE_TXT;
    txt_rec[0].data.txt.key = mdns_string_t{ "node_id", 7 };
    txt_rec[0].data.txt.value = mdns_string_t{ adv_node_id_.c_str(), adv_node_id_.size() };

    txt_rec[1].name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };
    txt_rec[1].type = MDNS_RECORDTYPE_TXT;
    txt_rec[1].data.txt.key = mdns_string_t{ "payload", 7 };
    txt_rec[1].data.txt.value = mdns_string_t{ adv_payload_json_.c_str(), adv_payload_json_.size() };

    // A/AAAA records: one per IP
    mdns_record_t additional[32]{};
    size_t add_count = 0;

    // Always include SRV and TXT in additional (helps scanners)
    additional[add_count++] = srv;
    additional[add_count++] = txt_rec[0];
    additional[add_count++] = txt_rec[1];

    // Add A/AAAA for host_qualified
    for (auto const& ip : adv_ips_) {
      if (add_count >= (sizeof(additional)/sizeof(additional[0]))) break;

      sockaddr_in a4{};
      sockaddr_in6 a6{};
      if (inet_pton(AF_INET, ip.c_str(), &a4.sin_addr) == 1) {
        a4.sin_family = AF_INET;
        mdns_record_t rec{};
        rec.name = mdns_string_t{ adv_host_qualified_.c_str(), adv_host_qualified_.size() };
        rec.type = MDNS_RECORDTYPE_A;
        rec.data.a.addr = a4;
        additional[add_count++] = rec;
      } else if (inet_pton(AF_INET6, ip.c_str(), &a6.sin6_addr) == 1) {
        a6.sin6_family = AF_INET6;
        mdns_record_t rec{};
        rec.name = mdns_string_t{ adv_host_qualified_.c_str(), adv_host_qualified_.size() };
        rec.type = MDNS_RECORDTYPE_AAAA;
        rec.data.aaaa.addr = a6;
        additional[add_count++] = rec;
      }
    }

    // Decide what to answer based on qname/rtype (mdns.c logic)
    const bool q_is_service = (ends_with_case_insensitive(qn, service_type_) &&
                               ends_with_case_insensitive(service_type_, ".local."));
    const bool q_is_instance = ends_with_case_insensitive(qn, service_type_);

    // If question is PTR for our service type -> answer PTR + additional SRV/TXT/A/AAAA
    if ((rtype == MDNS_RECORDTYPE_PTR || rtype == MDNS_RECORDTYPE_ANY) &&
        ends_with_case_insensitive(qn, service_type_)) {

      if (unicast) {
        mdns_query_answer_unicast(sock, from, addrlen, sendbuf_, sizeof(sendbuf_),
                                  0 /*query_id*/, (mdns_record_type_t)rtype,
                                  qname.str, qname.length,
                                  ptr, nullptr, 0, additional, add_count);
      } else {
        mdns_query_answer_multicast(sock, sendbuf_, sizeof(sendbuf_),
                                    ptr, nullptr, 0, additional, add_count);
      }
      return;
    }

    // SRV/TXT question for our instance
    if (q_is_instance && (to_lower(qn) == to_lower(adv_instance_))) {
      if ((rtype == MDNS_RECORDTYPE_SRV || rtype == MDNS_RECORDTYPE_ANY)) {
        mdns_record_t answer = srv;
        if (unicast) {
          mdns_query_answer_unicast(sock, from, addrlen, sendbuf_, sizeof(sendbuf_),
                                    0, (mdns_record_type_t)rtype,
                                    qname.str, qname.length,
                                    answer, nullptr, 0, additional, add_count);
        } else {
          mdns_query_answer_multicast(sock, sendbuf_, sizeof(sendbuf_),
                                      answer, nullptr, 0, additional, add_count);
        }
        return;
      }

      if ((rtype == MDNS_RECORDTYPE_TXT || rtype == MDNS_RECORDTYPE_ANY)) {
        // TXT is in additional already; answer with SRV or PTR is enough,
        // but if explicitly asked TXT, we can answer with one TXT record and additional.
        mdns_record_t answer = txt_rec[0]; // library will coalesce with txt_rec[1] if both in additional
        if (unicast) {
          mdns_query_answer_unicast(sock, from, addrlen, sendbuf_, sizeof(sendbuf_),
                                    0, (mdns_record_type_t)rtype,
                                    qname.str, qname.length,
                                    answer, nullptr, 0, additional, add_count);
        } else {
          mdns_query_answer_multicast(sock, sendbuf_, sizeof(sendbuf_),
                                      answer, nullptr, 0, additional, add_count);
        }
        return;
      }
    }

    // A/AAAA question for our host_qualified
    if (to_lower(ensure_trailing_dot(qn)) == to_lower(adv_host_qualified_)) {
      // Find first matching record type in additional and answer it (plus additional)
      for (size_t i = 0; i < add_count; ++i) {
        if ((rtype == MDNS_RECORDTYPE_A && additional[i].type == MDNS_RECORDTYPE_A) ||
            (rtype == MDNS_RECORDTYPE_AAAA && additional[i].type == MDNS_RECORDTYPE_AAAA) ||
            (rtype == MDNS_RECORDTYPE_ANY)) {

          mdns_record_t answer = additional[i];
          if (unicast) {
            mdns_query_answer_unicast(sock, from, addrlen, sendbuf_, sizeof(sendbuf_),
                                      0, (mdns_record_type_t)rtype,
                                      qname.str, qname.length,
                                      answer, nullptr, 0, additional, add_count);
          } else {
            mdns_query_answer_multicast(sock, sendbuf_, sizeof(sendbuf_),
                                        answer, nullptr, 0, additional, add_count);
          }
          return;
        }
      }
    }
  }


    // ---- client side: parse ANSWERS/AUTHORITY/ADDITIONAL ----
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
      (void)rclass;

      // Service side: answer QUESTIONS
      if (entry == MDNS_ENTRYTYPE_QUESTION) {
        // (Optional) add debug in handle_question_ instead if you prefer.
        handle_question_(sock, from, addrlen, rtype, rclass, ttl, data, size, name_offset);
        return;
      }

      // Client side: parse ANSWER/AUTHORITY/ADDITIONAL
      if (entry != MDNS_ENTRYTYPE_ANSWER &&
          entry != MDNS_ENTRYTYPE_AUTHORITY &&
          entry != MDNS_ENTRYTYPE_ADDITIONAL)
        return;

      // Extract owner/record name
      char namebuf[256] = {0};
      size_t nofs = name_offset;
      mdns_string_t owner = mdns_string_extract(data, size, &nofs, namebuf, sizeof(namebuf));
      if (!owner.str || !owner.length) return;

      const std::string owner_name(owner.str, owner.length);

      const uint64_t now = now_ms();
      const uint64_t expire_ms = (ttl == 0) ? 0 : (now + (uint64_t)ttl * 1000ULL);

      if (rtype == MDNS_RECORDTYPE_PTR) {
        // PTR: service_type -> instance
        char instbuf[256] = {0};
        mdns_string_t inst = mdns_record_parse_ptr(data, size, record_offset, record_length,
                                                  instbuf, sizeof(instbuf));
        if (!inst.str || !inst.length) return;

        const std::string instance(inst.str, inst.length);

        if (!ends_with_case_insensitive(owner_name, service_type_)) {
          return;
        }
        if (!ends_with_case_insensitive(instance, service_type_)) {
          return;
        }

        std::string node_id = node_id_from_instance(instance);
        if (node_id.empty()) {
          return;
        }

        std::lock_guard<std::mutex> lk(mtx_);

        if (ttl == 0) {
          nodes_.erase(node_id);
          instance_to_node_.erase(instance);
          expire_ms_by_node_.erase(node_id);
          cv_.notify_all();
          return;
        }

        NodeInfo& ni = nodes_[node_id];
        ni.node_id = node_id;
        ni.instance_name = instance;

        instance_to_node_[instance] = node_id;
        expire_ms_by_node_[node_id] = expire_ms;
        cv_.notify_all();

        // allow refresh logic to query it soon
        queried_instance_.erase(instance);
        return;
      }

      if (rtype == MDNS_RECORDTYPE_SRV) {
        // SRV: instance -> host_qualified + port
        char hostbuf[256] = {0};
        mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
                                                      hostbuf, sizeof(hostbuf));
        if (!srv.name.str || !srv.name.length) return;

        const std::string instance = ensure_trailing_dot(owner_name);
        if (!ends_with_case_insensitive(instance, service_type_)) {
          return;
        }

        const std::string host_str(srv.name.str, srv.name.length);

        std::string node_id;
        {
          std::lock_guard<std::mutex> lk(mtx_);
          auto it = instance_to_node_.find(instance);
          node_id = (it != instance_to_node_.end()) ? it->second : node_id_from_instance(instance);

          if (node_id.empty()) {
            return;
          }

          if (ttl == 0) {
            nodes_.erase(node_id);
            instance_to_node_.erase(instance);
            expire_ms_by_node_.erase(node_id);
            cv_.notify_all();
            return;
          }

          NodeInfo& ni = nodes_[node_id];
          ni.node_id = node_id;
          ni.instance_name = instance;
          ni.host_qualified = host_str;
          ni.port = (uint16_t)srv.port;

          std::string host_norm = norm_host(ni.host_qualified);
          host_to_node_[host_norm] = node_id;

          // Apply pending IPs already seen for that host
          auto pit = pending_ips_by_host_.find(host_norm);
          if (pit != pending_ips_by_host_.end()) {
            for (auto& ip : pit->second) {
              if (std::find(ni.ips.begin(), ni.ips.end(), ip) == ni.ips.end())
                ni.ips.push_back(ip);
            }
            pending_ips_by_host_.erase(pit);
          }

          expire_ms_by_node_[node_id] = expire_ms;
          cv_.notify_all();
        }

        // allow refresh logic to query it soon
        queried_host_.erase(norm_host(host_str));
        return;
      }

      if (rtype == MDNS_RECORDTYPE_TXT) {
        // TXT: instance -> key/value pairs; use node_id + payload
        mdns_record_txt_t txt[16];
        size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length,
                                              txt, sizeof(txt)/sizeof(txt[0]));

        const std::string instance = ensure_trailing_dot(owner_name);
        if (!ends_with_case_insensitive(instance, service_type_)) {
          return;
        }

        // Start with canonical node id from instance (lowercase)
        std::string node_id = node_id_from_instance(instance);
        std::string payload = "{}";

        // If TXT provides node_id, it might be uppercase -> MUST canonicalize to match PTR/SRV/A keys
        std::string txt_node_id_raw;

        for (size_t i = 0; i < parsed; ++i) {
          std::string key(txt[i].key.str ? txt[i].key.str : "", txt[i].key.length);
          std::string val(txt[i].value.str ? txt[i].value.str : "", txt[i].value.length);

          // Logger::debug("ZconfDiscovery: TXT kv key='{}' val_len={}", key, (int)val.size());

          if (key == "node_id" && !val.empty()) txt_node_id_raw = val;
          if (key == "payload" && !val.empty()) payload = val;
        }

        if (!txt_node_id_raw.empty()) {
          node_id = to_lower(txt_node_id_raw);
        }

        if (node_id.empty()) {
          return;
        }

        std::lock_guard<std::mutex> lk(mtx_);
        if (ttl == 0) {
          nodes_.erase(node_id);
          instance_to_node_.erase(instance);
          expire_ms_by_node_.erase(node_id);
          cv_.notify_all();
          return;
        }

        NodeInfo& ni = nodes_[node_id];
        ni.node_id = node_id;
        ni.instance_name = instance;
        ni.payload_json = payload;

        expire_ms_by_node_[node_id] = expire_ms;
        cv_.notify_all();
        return;
      }

      if (rtype == MDNS_RECORDTYPE_A) {
        // Logger::debug("ZconfDiscovery: HIT A owner='{}'", owner_name);

        // A: host_qualified -> IPv4
        sockaddr_in addr{};
        mdns_record_parse_a(data, size, record_offset, record_length, &addr);
        std::string ip = ipv4_to_string(addr);
        if (ip.empty()) return;

        const std::string host_norm = norm_host(owner_name);

        std::lock_guard<std::mutex> lk(mtx_);

        auto itNode = host_to_node_.find(host_norm);
        if (itNode == host_to_node_.end()) {
          auto& v = pending_ips_by_host_[host_norm];
          if (std::find(v.begin(), v.end(), ip) == v.end()) v.push_back(ip);
          return;
        }

        const std::string& node_id = itNode->second;
        auto it = nodes_.find(node_id);
        if (it == nodes_.end()) return;

        auto& ips = it->second.ips;
        if (ttl == 0) {
          ips.erase(std::remove(ips.begin(), ips.end(), ip), ips.end());
        } else {
          if (std::find(ips.begin(), ips.end(), ip) == ips.end()) ips.push_back(ip);
          expire_ms_by_node_[node_id] = expire_ms;
        }
        cv_.notify_all();
        return;
      }

      if (rtype == MDNS_RECORDTYPE_AAAA) {
        // AAAA: host_qualified -> IPv6
        sockaddr_in6 addr6{};
        mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr6);
        std::string ip = ipv6_to_string(addr6);
        if (ip.empty()) return;

        const std::string host_norm = norm_host(owner_name);

        std::lock_guard<std::mutex> lk(mtx_);

        auto itNode = host_to_node_.find(host_norm);
        if (itNode == host_to_node_.end()) {
          auto& v = pending_ips_by_host_[host_norm];
          if (std::find(v.begin(), v.end(), ip) == v.end()) v.push_back(ip);
          return;
        }

        const std::string& node_id = itNode->second;
        auto it = nodes_.find(node_id);
        if (it == nodes_.end()) return;

        auto& ips = it->second.ips;
        if (ttl == 0) {          
          ips.erase(std::remove(ips.begin(), ips.end(), ip), ips.end());
        } else {
          if (std::find(ips.begin(), ips.end(), ip) == ips.end()) ips.push_back(ip);
          expire_ms_by_node_[node_id] = expire_ms;
        }
        cv_.notify_all();
        return;
      }

    }


  // ---- send announce/goodbye (optional but good) ----
  void send_announce_locked_() {
    if (!advertising_) return;

    mdns_record_t ptr{};
    ptr.name = mdns_string_t{ service_type_.c_str(), service_type_.size() };
    ptr.type = MDNS_RECORDTYPE_PTR;
    ptr.data.ptr.name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };

    mdns_record_t srv{};
    srv.name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };
    srv.type = MDNS_RECORDTYPE_SRV;
    srv.data.srv.name = mdns_string_t{ adv_host_qualified_.c_str(), adv_host_qualified_.size() };
    srv.data.srv.port = adv_port_;
    srv.data.srv.priority = 0;
    srv.data.srv.weight = 0;

    mdns_record_t txt_rec[2]{};
    txt_rec[0].name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };
    txt_rec[0].type = MDNS_RECORDTYPE_TXT;
    txt_rec[0].data.txt.key = mdns_string_t{ "node_id", 7 };
    txt_rec[0].data.txt.value = mdns_string_t{ adv_node_id_.c_str(), adv_node_id_.size() };

    txt_rec[1].name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };
    txt_rec[1].type = MDNS_RECORDTYPE_TXT;
    txt_rec[1].data.txt.key = mdns_string_t{ "payload", 7 };
    txt_rec[1].data.txt.value = mdns_string_t{ adv_payload_json_.c_str(), adv_payload_json_.size() };

    mdns_record_t additional[32]{};
    size_t add_count = 0;
    additional[add_count++] = srv;
    additional[add_count++] = txt_rec[0];
    additional[add_count++] = txt_rec[1];

    for (auto const& ip : adv_ips_) {
      if (add_count >= (sizeof(additional)/sizeof(additional[0]))) break;

      sockaddr_in a4{};
      sockaddr_in6 a6{};
      if (inet_pton(AF_INET, ip.c_str(), &a4.sin_addr) == 1) {
        a4.sin_family = AF_INET;
        mdns_record_t rec{};
        rec.name = mdns_string_t{ adv_host_qualified_.c_str(), adv_host_qualified_.size() };
        rec.type = MDNS_RECORDTYPE_A;
        rec.data.a.addr = a4;
        additional[add_count++] = rec;
      } else if (inet_pton(AF_INET6, ip.c_str(), &a6.sin6_addr) == 1) {
        a6.sin6_family = AF_INET6;
        mdns_record_t rec{};
        rec.name = mdns_string_t{ adv_host_qualified_.c_str(), adv_host_qualified_.size() };
        rec.type = MDNS_RECORDTYPE_AAAA;
        rec.data.aaaa.addr = a6;
        additional[add_count++] = rec;
      }
    }

    for (int i = 0; i < n_service_; ++i) {
      mdns_announce_multicast(service_socks_[i], sendbuf_, sizeof(sendbuf_),
                              ptr, nullptr, 0, additional, add_count);
    }
  }

  void send_goodbye_locked_() {
    if (!advertising_) return;

    mdns_record_t ptr{};
    ptr.name = mdns_string_t{ service_type_.c_str(), service_type_.size() };
    ptr.type = MDNS_RECORDTYPE_PTR;
    ptr.data.ptr.name = mdns_string_t{ adv_instance_.c_str(), adv_instance_.size() };

    mdns_record_t additional[1]{};
    size_t add_count = 0;
    // (goodbye can include extra records too, but PTR with ttl=0 is the important part)

    for (int i = 0; i < n_service_; ++i) {
      mdns_goodbye_multicast(service_socks_[i], sendbuf_, sizeof(sendbuf_),
                             ptr, nullptr, 0, additional, add_count);
    }
  }

  void send_goodbye_if_needed_() {
    std::lock_guard<std::mutex> lk(adv_mtx_);
    if (advertising_) send_goodbye_locked_();
  }

  // ---- query senders ----
  void send_ptr_query_all_() {
    for (int i = 0; i < n_client_; ++i) {
      mdns_query_send(client_socks_[i], MDNS_RECORDTYPE_PTR,
                      service_type_.c_str(), service_type_.size(),
                      sendbuf_, sizeof(sendbuf_), 0);
    }
  }

  void send_instance_queries_(const std::string& instance) {
    // Ask SRV + TXT for instance
    mdns_query_t q[2];
    q[0].type = MDNS_RECORDTYPE_SRV; q[0].name = instance.c_str(); q[0].length = instance.size();
    q[1].type = MDNS_RECORDTYPE_TXT; q[1].name = instance.c_str(); q[1].length = instance.size();
    for (int i = 0; i < n_client_; ++i) {
      mdns_multiquery_send(client_socks_[i], q, 2, sendbuf_, sizeof(sendbuf_), 0);
    }
  }

  void send_host_queries_(const std::string& hostq) {
    // Ask A + AAAA for host
    mdns_query_t q[2];
    q[0].type = MDNS_RECORDTYPE_A;    q[0].name = hostq.c_str(); q[0].length = hostq.size();
    q[1].type = MDNS_RECORDTYPE_AAAA; q[1].name = hostq.c_str(); q[1].length = hostq.size();
    for (int i = 0; i < n_client_; ++i) {
      mdns_multiquery_send(client_socks_[i], q, 2, sendbuf_, sizeof(sendbuf_), 0);
    }
  }

  // ---- main loop ----
  void thread_main() {
    // sockets: service (listen on 5353) + client (ephemeral per interface)
    n_service_ = open_service_sockets(service_socks_, MAX_SOCKETS);
    n_client_  = open_client_sockets(client_socks_, MAX_SOCKETS, 0);

    Logger::debug("[ZconfDiscovery] service sockets=" + std::to_string(n_service_) +
                  " client sockets=" + std::to_string(n_client_));

    last_ptr_query_ms_ = 0;
    last_refresh_ms_ = 0;

    while (!closing_) {
      // Periodic PTR discovery
      const uint64_t t = now_ms();
      if (t - last_ptr_query_ms_ > 1500) {
        send_ptr_query_all_();
        last_ptr_query_ms_ = t;
      }

      // Follow-up queries: any new instances? any hosts?
      if (t - last_refresh_ms_ > 500) {
        std::vector<std::string> instances;
        std::vector<std::string> hosts;

        {
          std::lock_guard<std::mutex> lk(mtx_);
          for (auto const& kv : nodes_) {
            if (!kv.second.instance_name.empty()) {
              instances.push_back(ensure_trailing_dot(kv.second.instance_name));
            }
            if (!kv.second.host_qualified.empty()) {
              hosts.push_back(ensure_trailing_dot(kv.second.host_qualified));
            }
          }
        }

        for (auto& inst : instances) {
          if (queried_instance_.insert(inst).second) {
            send_instance_queries_(inst);
          }
        }
        for (auto& h : hosts) {
          const std::string hn = norm_host(h);
          if (queried_host_.insert(hn).second) {
            send_host_queries_(ensure_trailing_dot(h));
          }
        }

        last_refresh_ms_ = t;
      }

      // Announce occasionally (not required if answering QUESTIONS, but helps)
      {
        std::lock_guard<std::mutex> lk(adv_mtx_);
        static uint64_t last_announce = 0;
        if (advertising_ && (t - last_announce > 2000)) {
          send_announce_locked_();
          last_announce = t;
        }
      }

      // select() on all sockets (service + client) like mdns.c
      fd_set readfs;
      FD_ZERO(&readfs);
      int nfds = 0;

      for (int i = 0; i < n_service_; ++i) {
        FD_SET(service_socks_[i], &readfs);
        nfds = std::max(nfds, service_socks_[i] + 1);
      }
      for (int i = 0; i < n_client_; ++i) {
        FD_SET(client_socks_[i], &readfs);
        nfds = std::max(nfds, client_socks_[i] + 1);
      }

      timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 100000; // 100ms

      const int r = select(nfds, &readfs, nullptr, nullptr, &tv);
      if (r > 0) {
        for (int i = 0; i < n_service_; ++i) {
          if (FD_ISSET(service_socks_[i], &readfs)) {
            mdns_socket_listen(service_socks_[i], recvbuf_, sizeof(recvbuf_), &Impl::mdns_callback, this);
          }
        }
        for (int i = 0; i < n_client_; ++i) {
          if (FD_ISSET(client_socks_[i], &readfs)) {
            // Parse all replies (no query_id filtering, like mdns.c can do)
            mdns_query_recv(client_socks_[i], recvbuf_, sizeof(recvbuf_), &Impl::mdns_callback, this, 0);
          }
        }
      }
    }
  }
};

// ---- ZconfDiscovery wrapper ----

ZconfDiscovery::ZconfDiscovery(std::string service_type)
  : impl_(new Impl(std::move(service_type))) {}

ZconfDiscovery::~ZconfDiscovery() {
  close();
}

void ZconfDiscovery::start() { impl_->start(); }
void ZconfDiscovery::close() { impl_->close(); }

void ZconfDiscovery::advertise(const std::string& node_id,
                               std::uint16_t port,
                               const std::string& payload_json,
                               const std::vector<std::string>& ips) {
  impl_->advertise(node_id, port, payload_json, ips);
}

void ZconfDiscovery::stop_advertising() { impl_->stop_advertising(); }

std::vector<ZconfDiscovery::NodeInfo> ZconfDiscovery::list_nodes() const {
  return impl_->list_nodes();
}

bool ZconfDiscovery::resolve_node(const std::string& node_id, NodeInfo& out, double timeout_sec) const {
  return impl_->resolve_node(node_id, out, timeout_sec);
}

std::string ZconfDiscovery::pick_best_ip(const NodeInfo& node) {
  for (const auto& ip : node.ips) {
    if (ip.rfind("127.", 0) == 0) continue;
    if (ip.rfind("169.254.", 0) == 0) continue;
    return ip;
  }
  return node.ips.empty() ? "" : node.ips.front();
}

} // namespace magpie
