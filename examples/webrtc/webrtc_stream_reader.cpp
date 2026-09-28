//
// webrtc_stream_reader.cpp
//
// Connects to a remote peer via WebRTC and prints
// every StringFrame received on "magpie/test/topic".
//
// Build:  cmake -DMAGPIE_WITH_WEBRTC=ON ..
// Run:    ./example_webrtc_stream_reader http://127.0.0.1:8000/signal
//
// Pair with: example_webrtc_stream_writer (or the Python/JS equivalent)
// Both sides must use the same signaling address and session ID.
//

#include <magpie/frames/primitive_frames.hpp>
#include "example_signaling.hpp"
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_stream_reader.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <memory>

int main(int argc, char** argv) {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ------------------------------------------------------------------
    // 1. Select signaling transport
    // ------------------------------------------------------------------
    const std::string address = argc > 1 ? argv[1] : "http://127.0.0.1:8000/signal";
    auto signaler = exampleSignaler(address, "magpie-cpp-demo", argc > 2 && std::string(argv[2]) == "--bind");

    // ------------------------------------------------------------------
    // 2. Create WebRTC connection and wait for peer
    // ------------------------------------------------------------------
    auto conn = std::make_shared<WebRtcConnection>(signaler);

    Logger::info("Waiting for peer (session: magpie-cpp-demo) ...");
    if (!conn->connect(30.0)) {
        Logger::error("No peer found within 30s — is the writer running?");
        return 1;
    }
    Logger::info("Connected! Subscribing to 'magpie/test/topic'.");

    // ------------------------------------------------------------------
    // 3. Create reader and receive frames
    // ------------------------------------------------------------------
    WebRtcStreamReader sub(conn, "magpie/test/topic");

    while (conn->isConnected()) {
        std::unique_ptr<Frame> frame;
        std::string topic;

        try {
            if (sub.read(frame, topic, /*timeoutSec=*/5.0)) {
                auto* sf = dynamic_cast<StringFrame*>(frame.get());
                if (sf) {
                    Logger::info("Reader [" + topic + "]: '" + sf->value() + "'");
                } else if (auto* df = dynamic_cast<DictFrame*>(frame.get())) {
                    Logger::info("Reader [" + topic + "]: " +
                                 Value::fromDict(df->value()).toDebugString());
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
