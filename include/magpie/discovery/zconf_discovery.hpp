#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace magpie {

/**
 * @brief ZconfDiscovery (Avahi backend)
 *
 * C++ equivalent of your Python ZconfDiscovery:
 *  - Advertise a DNS-SD/mDNS service with TXT keys: node_id, proto, payload
 *  - Browse and resolve other services of the same type
 *  - Maintain a node registry (node_id -> NodeInfo)
 *  - Allow blocking resolveNode() with timeout
 *
 * Notes:
 *  - Uses AvahiThreadedPoll to run Avahi's event loop in a helper thread.
 *  - Thread-safety: Avahi objects are accessed under avahi_threaded_poll_lock/unlock.
 *  - Interoperability: TXT keys and payload JSON are kept identical in concept to Python.
 */
class ZconfDiscovery {
public:
    struct NodeInfo {
        std::string nodeId;
        std::string serviceName;     // instance name in DNS-SD
        std::string serviceType;     // e.g. "_magpie-zmq._tcp"
        std::string domain;          // usually "local"
        std::string hostName;        // resolved host name (may be empty)
        std::vector<std::string> ips;
        std::uint16_t port = 0;

        std::string proto;           // e.g. "zmq"
        std::string payload;         // JSON string
        std::map<std::string, std::string> txt;  // all TXT pairs

        bool isResolved() const { return !ips.empty() && port != 0; }
    };

public:
    /**
     * @param serviceType  DNS-SD service type, e.g. "_magpie-zmq._tcp"
     * @param domain       typically "local"
     */
    explicit ZconfDiscovery(std::string serviceType = "_magpie-zmq._tcp",
                            std::string domain = "local");
    ~ZconfDiscovery();

    ZconfDiscovery(const ZconfDiscovery&)            = delete;
    ZconfDiscovery& operator=(const ZconfDiscovery&) = delete;

    /**
     * @brief Start browsing in the background (idempotent).
     * You can call advertise() without calling start(), but start() is recommended.
     */
    void start();

    /**
     * @brief Stop browsing and unpublish any advertised service (idempotent).
     */
    void close();

    /**
     * @brief Advertise this node.
     *
     * @param nodeId   logical node id (TXT: node_id)
     * @param port     service port
     * @param proto    protocol name (TXT: proto), e.g. "zmq"
     * @param payload  JSON string (TXT: payload)
     * @param instanceName optional DNS-SD instance name. If empty, uses nodeId.
     */
    void advertise(const std::string& nodeId,
                   std::uint16_t port,
                   const std::string& proto,
                   const std::string& payload,
                   const std::string& instanceName = "");

    /**
     * @brief Remove currently advertised service (if any).
     */
    void unadvertise();

    /**
     * @brief Get a snapshot list of all currently known nodes.
     */
    std::vector<NodeInfo> listNodes() const;

    /**
     * @brief Try get a known node immediately (no waiting). Returns false if missing.
     */
    bool tryGetNode(const std::string& nodeId, NodeInfo& out) const;

    /**
     * @brief Resolve a node (wait until it appears/resolves or timeout).
     *
     * @param nodeId node id
     * @param timeoutSec <0 => wait forever; >=0 => wait up to timeoutSec then return false
     * @return true if resolved and written to out
     */
    bool resolveNode(const std::string& nodeId, NodeInfo& out, double timeoutSec = 2.0) const;

    /**
     * @brief Heuristic to choose best IP from a list (prefers non-loopback/non-link-local).
     */
    static std::string pickBestIp(const std::vector<std::string>& ips);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace magpie
