//
// mqtt_publisher.cpp
//
// Publishes a StringFrame to an MQTT broker every second.
// Connects to the public HiveMQ broker by default.
//
// Build:  cmake -DMAGPIE_WITH_MQTT=ON ..
// Run:    ./example_mqtt_publisher
//
// Pair with: example_mqtt_subscriber
//

#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_publisher.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

static volatile bool running = true;

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ------------------------------------------------------------------
    // 1. Create a shared connection to the broker
    // ------------------------------------------------------------------
    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect(10.0);

    // ------------------------------------------------------------------
    // 2. Create a publisher (queueSize=10: async background thread)
    // ------------------------------------------------------------------
    MqttPublisher pub(conn, /*serializer=*/nullptr, /*queueSize=*/10);

    Logger::info("MQTT publisher started. Publishing to 'magpie/test/topic'.");

    int count = 0;
    while (running) {
        StringFrame frame("hello from C++ #" + std::to_string(count++));
        Logger::info("Publisher: sending '" + frame.value() + "'");
        pub.write(frame, "magpie/test/topic");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    pub.close();
    conn->disconnect();
    return 0;
}
