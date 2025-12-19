#pragma once

#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace magpie {

class ZconfDiscovery {
public:
  struct NodeInfo {
    std::string node_id;
    std::vector<std::string> ips;   // IPv4 + IPv6 (as strings) if available
    std::uint16_t port = 0;
    std::string payload_json = "{}";

    // Internal-ish fields for debugging/consistency (kept public for convenience)
    std::string instance_name;      // "<node_id>.<service_type>"
    std::string host_qualified;     // "<hostname>.local."
    bool is_resolved() const { return (port != 0) && !ips.empty(); }
  };

  explicit ZconfDiscovery(std::string service_type = "_magpie-zmq._tcp.local.");
  ~ZconfDiscovery();

  void start();
  void close();

  // Advertise this node (like python advertise_node, you renamed to advertise)
  void advertise(const std::string& node_id,
                 std::uint16_t port,
                 const std::string& payload_json = "{}");

  void stop_advertising();

  // C++14-friendly: returns bool and fills out param
  bool resolve_node(const std::string& node_id,
                    NodeInfo& out,
                    double timeout_sec = 5.0) const;

  std::vector<NodeInfo> list_nodes() const;

  static std::string pick_best_ip(const NodeInfo& node);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace magpie
