#include <magpie/discovery/zconf_discovery.hpp>
#include <magpie/utils/logger.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Avahi headers
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>

namespace magpie {

struct ZconfDiscovery::Impl {
    explicit Impl(std::string st, std::string dom)
        : serviceType(std::move(st)), domain(std::move(dom)) {}

    ~Impl() {
        // best-effort
        try { shutdown_(); } catch (...) {}
    }

    // -----------------------------
    // Public config/state
    // -----------------------------
    std::string serviceType;
    std::string domain;

    std::atomic<bool> started{false};
    std::atomic<bool> closing{false};

    // -----------------------------
    // Avahi objects (owned by poll thread)
    // -----------------------------
    AvahiSimplePoll* simplePoll = nullptr;
    AvahiClient* client         = nullptr;
    AvahiServiceBrowser* browser = nullptr;
    AvahiEntryGroup* group       = nullptr;

    // -----------------------------
    // Advertise state (mirrors Python)
    // -----------------------------
    std::string advNodeId;
    std::string advInstance;
    std::uint16_t advPort = 0;
    std::string advProto;
    std::string advPayload;
    bool advRequested = false;

    // -----------------------------
    // Node registry
    // -----------------------------
    mutable std::mutex nodesMtx;
    mutable std::condition_variable nodesCv;
    std::unordered_map<std::string, NodeInfo> nodes; // node_id -> info

    // -----------------------------
    // Command queue for cross-thread API calls
    // -----------------------------
    enum class CmdType { Publish, Unpublish, Stop };

    struct Cmd {
        CmdType type;
        // for Publish
        std::string nodeId;
        std::string instance;
        std::uint16_t port = 0;
        std::string proto;
        std::string payload;
    };

    mutable std::mutex cmdMtx;
    std::queue<Cmd> cmds;

    std::thread worker;

    // -----------------------------
    // TXT helpers
    // -----------------------------
    static std::map<std::string, std::string> parseTxt(AvahiStringList* txt) {
        std::map<std::string, std::string> out;
        for (AvahiStringList* l = txt; l; l = avahi_string_list_get_next(l)) {
            char* key = nullptr;
            char* val = nullptr;
            size_t size = 0;
            if (avahi_string_list_get_pair(l, &key, &val, &size) == 0) {
                std::string k = key ? key : "";
                std::string v;
                if (val) v.assign(val, size);
                out[k] = v;
                if (key) avahi_free(key);
                if (val) avahi_free(val);
            }
        }
        return out;
    }

    static std::string addrToString(const AvahiAddress* a) {
        char buf[AVAHI_ADDRESS_STR_MAX]{0};
        avahi_address_snprint(buf, sizeof(buf), a);
        return std::string(buf);
    }

    // -----------------------------
    // Avahi callbacks (run in poll thread)
    // -----------------------------
    static void clientCb(AvahiClient* c, AvahiClientState state, void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        self->onClientState_(c, state);
    }

    static void browseCb(AvahiServiceBrowser* b,
                         AvahiIfIndex interface,
                         AvahiProtocol protocol,
                         AvahiBrowserEvent event,
                         const char* name,
                         const char* type,
                         const char* domain,
                         AvahiLookupResultFlags flags,
                         void* userdata)
    {
        (void)b; (void)flags;
        auto* self = static_cast<Impl*>(userdata);
        self->onBrowse_(interface, protocol, event, name, type, domain);
    }

    struct ResolveCtx {
        Impl* self;
    };

    static void resolveCb(AvahiServiceResolver* r,
                          AvahiIfIndex interface,
                          AvahiProtocol protocol,
                          AvahiResolverEvent event,
                          const char* name,
                          const char* type,
                          const char* domain,
                          const char* host_name,
                          const AvahiAddress* a,
                          std::uint16_t port,
                          AvahiStringList* txt,
                          AvahiLookupResultFlags flags,
                          void* userdata)
    {
        (void)interface; (void)protocol; (void)flags;
        auto* ctx = static_cast<ResolveCtx*>(userdata);
        ctx->self->onResolve_(event, name, type, domain, host_name, a, port, txt);

        if (r) avahi_service_resolver_free(r);
        delete ctx;
    }

    // -----------------------------
    // Poll thread lifecycle
    // -----------------------------
    void startThread_() {
        if (started.exchange(true)) return;

        worker = std::thread([this]() { this->threadMain_(); });
    }

    void shutdown_() {
        if (!started.load()) return;
        if (closing.exchange(true)) return;

        // enqueue Stop
        {
            std::lock_guard<std::mutex> lk(cmdMtx);
            cmds.push(Cmd{CmdType::Stop});
        }
        // wake poll if running
        if (simplePoll) avahi_simple_poll_wakeup(simplePoll);

        if (worker.joinable()) worker.join();

        started.store(false);
    }

    void threadMain_() {
        simplePoll = avahi_simple_poll_new();
        if (!simplePoll) {
            Logger::warning("ZconfDiscovery: avahi_simple_poll_new failed");
            return;
        }

        int error = 0;
        client = avahi_client_new(
            avahi_simple_poll_get(simplePoll),
            AVAHI_CLIENT_NO_FAIL,
            &Impl::clientCb,
            this,
            &error
        );

        if (!client) {
            Logger::warning(std::string("ZconfDiscovery: avahi_client_new failed: ") + avahi_strerror(error));
            avahi_simple_poll_free(simplePoll);
            simplePoll = nullptr;
            return;
        }

        // Main loop: iterate + process commands
        while (!closing.load()) {
            // 1) handle queued commands
            processCommands_();

            // 2) iterate avahi loop (100ms tick)
            avahi_simple_poll_iterate(simplePoll, 100);
        }

        // final cleanup in poll thread
        cleanupAvahi_();

        avahi_simple_poll_free(simplePoll);
        simplePoll = nullptr;
    }

    void cleanupAvahi_() {
        // free in safe order
        if (browser) {
            avahi_service_browser_free(browser);
            browser = nullptr;
        }
        if (group) {
            avahi_entry_group_free(group);
            group = nullptr;
        }
        if (client) {
            avahi_client_free(client);
            client = nullptr;
        }
    }

    // -----------------------------
    // Command processing (poll thread)
    // -----------------------------
    void processCommands_() {
        for (;;) {
            Cmd cmd;
            {
                std::lock_guard<std::mutex> lk(cmdMtx);
                if (cmds.empty()) break;
                cmd = std::move(cmds.front());
                cmds.pop();
            }

            if (cmd.type == CmdType::Stop) {
                closing.store(true);
                break;
            }

            if (cmd.type == CmdType::Unpublish) {
                unpublish_();
                continue;
            }

            if (cmd.type == CmdType::Publish) {
                advNodeId     = cmd.nodeId;
                advInstance   = cmd.instance;
                advPort       = cmd.port;
                advProto      = cmd.proto;
                advPayload    = cmd.payload;
                advRequested  = true;

                // If client already running, publish now; otherwise onClientState_ will publish.
                if (client && avahi_client_get_state(client) == AVAHI_CLIENT_S_RUNNING) {
                    try { publish_(); }
                    catch (const std::exception& e) {
                        Logger::warning(std::string("ZconfDiscovery: publish failed: ") + e.what());
                    }
                }
                continue;
            }
        }
    }

    // -----------------------------
    // Avahi logic (poll thread only)
    // -----------------------------
    void onClientState_(AvahiClient* c, AvahiClientState state) {
        if (closing.load()) return;

        if (state == AVAHI_CLIENT_S_RUNNING) {
            Logger::debug("ZconfDiscovery: Avahi client is running.");

            // Start browsing
            if (!browser) {
                browser = avahi_service_browser_new(
                    c,
                    AVAHI_IF_UNSPEC,
                    AVAHI_PROTO_UNSPEC,
                    serviceType.c_str(),
                    domain.c_str(),
                    (AvahiLookupFlags)0,
                    &Impl::browseCb,
                    this
                );

                if (!browser) {
                    Logger::warning(std::string("ZconfDiscovery: avahi_service_browser_new failed: ")
                                    + avahi_strerror(avahi_client_errno(c)));
                }
            }

            // Publish if requested
            if (advRequested) {
                try { publish_(); }
                catch (const std::exception& e) {
                    Logger::warning(std::string("ZconfDiscovery: advertise failed: ") + e.what());
                }
            }
        }
        else if (state == AVAHI_CLIENT_FAILURE) {
            Logger::warning(std::string("ZconfDiscovery: Avahi client failure: ")
                            + avahi_strerror(avahi_client_errno(c)));
        }
    }

    void onBrowse_(AvahiIfIndex interface,
                   AvahiProtocol protocol,
                   AvahiBrowserEvent event,
                   const char* name,
                   const char* type,
                   const char* dom)
    {
        if (closing.load()) return;

        const std::string svcName = name ? name : "";
        const std::string svcType = type ? type : "";
        const std::string svcDom  = dom ? dom : "";

        if (event == AVAHI_BROWSER_NEW) {
            auto* ctx = new ResolveCtx{this};

            AvahiServiceResolver* resolver = avahi_service_resolver_new(
                client,
                interface,
                protocol,
                svcName.c_str(),
                svcType.c_str(),
                svcDom.c_str(),
                AVAHI_PROTO_UNSPEC,
                (AvahiLookupFlags)0,
                &Impl::resolveCb,
                ctx
            );

            if (!resolver) {
                delete ctx;
                Logger::warning("ZconfDiscovery: avahi_service_resolver_new failed");
            }
        }
        else if (event == AVAHI_BROWSER_REMOVE) {
            {
                std::lock_guard<std::mutex> lk(nodesMtx);
                for (auto it = nodes.begin(); it != nodes.end();) {
                    if (it->second.serviceName == svcName) it = nodes.erase(it);
                    else ++it;
                }
            }
            nodesCv.notify_all();
        }
    }

    void onResolve_(AvahiResolverEvent event,
                    const char* name,
                    const char* type,
                    const char* dom,
                    const char* host_name,
                    const AvahiAddress* a,
                    std::uint16_t port,
                    AvahiStringList* txt)
    {
        if (closing.load()) return;
        if (event != AVAHI_RESOLVER_FOUND) return;

        NodeInfo info;
        info.serviceName = name ? name : "";
        info.serviceType = type ? type : "";
        info.domain      = dom ? dom : "";
        info.hostName    = host_name ? host_name : "";
        info.port        = port;

        if (a) info.ips.push_back(addrToString(a));
        info.txt = parseTxt(txt);

        auto itNode = info.txt.find("node_id");
        info.nodeId = (itNode != info.txt.end()) ? itNode->second : info.serviceName;

        auto itProto = info.txt.find("proto");
        if (itProto != info.txt.end()) info.proto = itProto->second;

        auto itPayload = info.txt.find("payload");
        if (itPayload != info.txt.end()) info.payload = itPayload->second;

        {
            std::lock_guard<std::mutex> lk(nodesMtx);
            auto& slot = nodes[info.nodeId];

            if (!slot.nodeId.empty()) {
                for (const auto& ip : info.ips) {
                    if (std::find(slot.ips.begin(), slot.ips.end(), ip) == slot.ips.end())
                        slot.ips.push_back(ip);
                }
                slot.serviceName = info.serviceName;
                slot.serviceType = info.serviceType;
                slot.domain      = info.domain;
                slot.hostName    = info.hostName;
                slot.port        = info.port;
                slot.proto       = info.proto;
                slot.payload     = info.payload;
                slot.txt         = info.txt;
            } else {
                slot = info;
            }
        }

        nodesCv.notify_all();
    }

    void publish_() {
        if (!client) return;

        if (!group) {
            group = avahi_entry_group_new(client, nullptr, nullptr);
            if (!group) {
                throw std::runtime_error("Avahi: avahi_entry_group_new failed");
            }
        }

        // Reset to keep behavior predictable on repeated advertise()
        avahi_entry_group_reset(group);

        AvahiStringList* txt = nullptr;
        txt = avahi_string_list_add_pair(txt, "node_id", advNodeId.c_str());
        txt = avahi_string_list_add_pair(txt, "proto",   advProto.c_str());
        txt = avahi_string_list_add_pair(txt, "payload", advPayload.c_str());

        const std::string instance = advInstance.empty() ? advNodeId : advInstance;

        const int r = avahi_entry_group_add_service_strlst(
            group,
            AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC,
            (AvahiPublishFlags)0,
            instance.c_str(),
            serviceType.c_str(),
            domain.c_str(),
            nullptr,
            advPort,
            txt
        );

        avahi_string_list_free(txt);

        if (r < 0) throw std::runtime_error(std::string("Avahi: add_service failed: ") + avahi_strerror(r));

        const int c = avahi_entry_group_commit(group);
        if (c < 0) throw std::runtime_error(std::string("Avahi: entry_group_commit failed: ") + avahi_strerror(c));

        Logger::debug("ZconfDiscovery: advertised node_id=" + advNodeId +
                      " type=" + serviceType + " port=" + std::to_string(advPort));
    }

    void unpublish_() {
        if (group) avahi_entry_group_reset(group);
        advRequested = false;
        advNodeId.clear();
        advInstance.clear();
        advProto.clear();
        advPayload.clear();
        advPort = 0;
    }
};

// ----------------- ZconfDiscovery public API -----------------

ZconfDiscovery::ZconfDiscovery(std::string serviceType, std::string domain)
    : impl_(new Impl(std::move(serviceType), std::move(domain))) {}

ZconfDiscovery::~ZconfDiscovery() {
    close();
    delete impl_;
    impl_ = nullptr;
}

void ZconfDiscovery::start() {
    impl_->startThread_();
}

void ZconfDiscovery::close() {
    if (!impl_) return;
    impl_->shutdown_();
}

void ZconfDiscovery::advertise(const std::string& nodeId,
                               std::uint16_t port,
                               const std::string& proto,
                               const std::string& payload,
                               const std::string& instanceName)
{
    start();

    Impl::Cmd cmd;
    cmd.type     = Impl::CmdType::Publish;
    cmd.nodeId   = nodeId;
    cmd.instance = instanceName;
    cmd.port     = port;
    cmd.proto    = proto;
    cmd.payload  = payload;

    {
        std::lock_guard<std::mutex> lk(impl_->cmdMtx);
        impl_->cmds.push(std::move(cmd));
    }

    if (impl_->simplePoll) avahi_simple_poll_wakeup(impl_->simplePoll);
}

void ZconfDiscovery::unadvertise() {
    if (!impl_) return;

    Impl::Cmd cmd;
    cmd.type = Impl::CmdType::Unpublish;

    {
        std::lock_guard<std::mutex> lk(impl_->cmdMtx);
        impl_->cmds.push(std::move(cmd));
    }

    if (impl_->simplePoll) avahi_simple_poll_wakeup(impl_->simplePoll);
}

std::vector<ZconfDiscovery::NodeInfo> ZconfDiscovery::listNodes() const {
    std::lock_guard<std::mutex> lk(impl_->nodesMtx);
    std::vector<NodeInfo> out;
    out.reserve(impl_->nodes.size());
    for (const auto& kv : impl_->nodes) out.push_back(kv.second);
    return out;
}

bool ZconfDiscovery::tryGetNode(const std::string& nodeId, NodeInfo& out) const {
    std::lock_guard<std::mutex> lk(impl_->nodesMtx);
    auto it = impl_->nodes.find(nodeId);
    if (it == impl_->nodes.end()) return false;
    out = it->second;
    return true;
}

bool ZconfDiscovery::resolveNode(const std::string& nodeId, NodeInfo& out, double timeoutSec) const {
    std::unique_lock<std::mutex> lk(impl_->nodesMtx);

    auto ready = [&]() -> bool {
        auto it = impl_->nodes.find(nodeId);
        if (it == impl_->nodes.end()) return false;
        return it->second.isResolved();
    };

    if (timeoutSec < 0.0) {
        impl_->nodesCv.wait(lk, ready);
    } else {
        using namespace std::chrono;
        impl_->nodesCv.wait_for(
            lk,
            duration_cast<milliseconds>(duration<double>(timeoutSec)),
            ready
        );
    }

    auto it = impl_->nodes.find(nodeId);
    if (it == impl_->nodes.end() || !it->second.isResolved()) return false;

    out = it->second;
    return true;
}

static bool isLoopback_(const std::string& ip) {
    return ip == "127.0.0.1" || ip == "::1";
}
static bool isLinkLocal_(const std::string& ip) {
    return ip.rfind("169.254.", 0) == 0 || ip.rfind("fe80:", 0) == 0;
}

std::string ZconfDiscovery::pickBestIp(const std::vector<std::string>& ips) {
    if (ips.empty()) return "";
    for (const auto& ip : ips) {
        if (!isLoopback_(ip) && !isLinkLocal_(ip)) return ip;
    }
    return ips.front();
}

} // namespace magpie
