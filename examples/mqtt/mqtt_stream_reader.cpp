//
// mqtt_stream_reader.cpp
//
// Subscribes to an MQTT topic and prints received StringFrames.
// Supports wildcards: '+' (single level) and '#' (multi-level).
//
// Build:  cmake -DMAGPIE_WITH_MQTT=ON ..
// Run:    ./example_mqtt_stream_reader
//
// Pair with: example_mqtt_stream_writer
//

#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_stream_reader.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <memory>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ------------------------------------------------------------------
    // 1. Connect to the broker
    // ------------------------------------------------------------------
    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect(10.0);

    // ------------------------------------------------------------------
    // 2. Subscribe to a topic (wildcard: receives everything under magpie/test/)
    // ------------------------------------------------------------------
    MqttStreamReader sub(conn, "magpie/test/+", /*serializer=*/nullptr, /*queueSize=*/10);

    Logger::info("MQTT reader started. Waiting for frames on 'magpie/test/+'...");

    while (true) {
        std::unique_ptr<Frame> frame;
        std::string topic;

        try {
            if (sub.read(frame, topic, /*timeoutSec=*/5.0)) {
                // Try to cast to StringFrame
                auto* sf = dynamic_cast<StringFrame*>(frame.get());
                if (sf) {
                    Logger::info("Reader [" + topic + "]: '" + sf->value() + "'");
                } else {
                    Logger::info("Reader [" + topic + "]: received frame type '" +
                                 frame->name() + "'");
                }
            }
        } catch (const TimeoutError&) {
            Logger::debug("Reader: timeout, still waiting...");
        }
    }

    sub.close();
    conn->disconnect();
    return 0;
}
