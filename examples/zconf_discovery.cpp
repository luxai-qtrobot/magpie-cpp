#include <magpie/discovery/zconf_discovery.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/utils/common.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace magpie;

static void advertiseNode() {
    const std::string nodeId = getUniqueId();
    const std::uint16_t port = 5555;
    const std::string payload = R"({"hello":"world"})";

    Logger::info("Advertising node_id=" + nodeId +
                 " on port=" + std::to_string(port) + " ...");

    ZconfDiscovery disc;
    disc.start();

    disc.advertise(
        nodeId,
        port,
        "zmq",
        payload
    );

    try {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    } catch (...) {
        // unreachable, loop exits via Ctrl+C
    }

    Logger::info("Advertiser shutting down...");
    disc.close();
}

static void scanNodes() {
    Logger::info("Starting Zeroconf node discovery...");

    ZconfDiscovery disc;
    disc.start();

    try {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            auto nodes = disc.listNodes();
            if (nodes.empty()) {
                Logger::debug("No nodes discovered...");
                continue;
            }

            Logger::info("Discovered nodes:");
            for (const auto& info : nodes) {
                const std::string bestIp = ZconfDiscovery::pickBestIp(info.ips);

                Logger::info(
                    "  node_id=" + info.nodeId +
                    "  ips=" + (info.ips.empty() ? "[]" : info.ips.front()) +
                    "  port=" + std::to_string(info.port) +
                    "  payload=" + info.payload +
                    "  (best: " + bestIp + ")"
                );
            }
        }
    } catch (...) {
        // Ctrl+C
    }

    Logger::info("Scanner shutting down...");
    disc.close();
}

int main(int argc, char** argv) {
    Logger::setLevel("DEBUG");

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " [advertise|scan]\n";
        return 1;
    }

    const std::string role = argv[1];

    if (role == "advertise") {
        advertiseNode();
    } else if (role == "scan") {
        scanNodes();
    } else {
        std::cerr << "Unknown role: " << role << "\n";
        return 1;
    }

    return 0;
}
