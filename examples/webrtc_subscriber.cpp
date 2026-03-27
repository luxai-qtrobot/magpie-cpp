//
// webrtc_subscriber.cpp
//
// Connects to a remote peer via WebRTC (signaling over MQTT) and prints
// every StringFrame received on "magpie/test/topic".
//
// Build:  cmake -DMAGPIE_WITH_WEBRTC=ON -DMAGPIE_WITH_MQTT=ON ..
// Run:    ./example_webrtc_subscriber
//
// Pair with: example_webrtc_publisher (or the Python/JS equivalent)
// Both sides must use the same broker URL and session_id.
//

#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_subscriber.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <memory>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ------------------------------------------------------------------
    // 1. Connect MQTT for signaling
    // ------------------------------------------------------------------
    auto signalConn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    signalConn->connect(10.0);

    // ------------------------------------------------------------------
    // 2. Create WebRTC connection and wait for peer
    // ------------------------------------------------------------------
    auto conn = std::make_shared<WebRtcConnection>(signalConn, "magpie-cpp-demo");

    Logger::info("Waiting for peer (session: magpie-cpp-demo) ...");
    if (!conn->connect(30.0)) {
        Logger::error("No peer found within 30s — is the publisher running?");
        signalConn->disconnect();
        return 1;
    }
    Logger::info("Connected! Subscribing to 'magpie/test/topic'.");

    // ------------------------------------------------------------------
    // 3. Create subscriber and receive frames
    // ------------------------------------------------------------------
    WebRtcSubscriber sub(conn, "magpie/test/topic");

    while (conn->isConnected()) {
        std::unique_ptr<Frame> frame;
        std::string topic;

        try {
            if (sub.read(frame, topic, /*timeoutSec=*/5.0)) {
                auto* sf = dynamic_cast<StringFrame*>(frame.get());
                if (sf) {
                    Logger::info("Subscriber [" + topic + "]: '" + sf->value() + "'");
                } else {
                    Logger::info("Subscriber [" + topic + "]: received frame type '" +
                                 frame->name() + "'");
                }
            }
        } catch (const TimeoutError&) {
            Logger::debug("Subscriber: timeout, still waiting...");
        }
    }

    sub.close();
    conn->disconnect();
    signalConn->disconnect();
    return 0;
}
