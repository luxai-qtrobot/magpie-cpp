#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace magpie {

class ZconfDiscovery {
public:
  struct NodeInfo {
    std::string node_id;
    std::string instance_name;   // "<node_id>.<service_type>"
    std::string host_qualified;  // "<node_id>.local."
    std::uint16_t port = 0;
    std::string payload_json = "{}";
    std::vector<std::string> ips;

    bool is_resolved() const {
      return !node_id.empty() && port != 0 && !ips.empty();
    }
  };

  explicit ZconfDiscovery(std::string service_type = "_magpie-zmq._tcp.local.");
  ~ZconfDiscovery();

  // Start/stop background thread (safe to call multiple times)
  void start();
  void close();

  // Advertise this process as a node on the LAN.
  // If ips is empty, implementation will enumerate local interfaces (recommended).
  void advertise(const std::string& node_id,
                 std::uint16_t port,
                 const std::string& payload_json = "{}",
                 const std::vector<std::string>& ips = {});

  void stop_advertising();

  // Discovery
  std::vector<NodeInfo> list_nodes() const;

  // Resolve one node into out; returns true on success within timeout.
  bool resolve_node(const std::string& node_id, NodeInfo& out, double timeout_sec = 5.0) const;

  // Helper
  static std::string pick_best_ip(const NodeInfo& node);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace magpie
