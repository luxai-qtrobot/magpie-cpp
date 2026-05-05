//
// webrtc_stream_writer.cpp
//
// Connects to a remote peer via WebRTC (signaling over MQTT) and publishes a
// StringFrame every second on the "magpie/test/topic" topic.
//
// Build:  cmake -DMAGPIE_WITH_WEBRTC=ON -DMAGPIE_WITH_MQTT=ON ..
// Run:    ./example_webrtc_stream_writer
//
// Pair with: example_webrtc_stream_reader (or the Python/JS equivalent)
// Both sides must use the same broker URL and session_id.
//

#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_stream_writer.hpp>
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
        Logger::error("No peer found within 30s — is the reader running?");
        signalConn->disconnect();
        return 1;
    }
    Logger::info("Connected! Publishing on 'magpie/test/topic'.");

    // ------------------------------------------------------------------
    // 3. Create writer and write at 1 Hz
    // ------------------------------------------------------------------
    WebRtcStreamWriter pub(conn);

    int count = 0;
    while (running && conn->isConnected()) {
        StringFrame frame("hello from C++ WebRTC #" + std::to_string(count++));
        Logger::info("Writer: sending '" + frame.value() + "'");
        pub.write(frame, "magpie/test/topic");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    pub.close();
    conn->disconnect();
    signalConn->disconnect();
    return 0;
}
