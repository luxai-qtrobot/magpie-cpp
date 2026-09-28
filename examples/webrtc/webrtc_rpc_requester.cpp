//
// webrtc_rpc_requester.cpp
//
// Performs RPC calls over a WebRTC data channel.
// Sends a Value dict request to the "magpie/echo" service and prints
// the response received from example_webrtc_rpc_responder.
//
// Build:  cmake -DMAGPIE_WITH_WEBRTC=ON ..
// Run:    ./example_webrtc_rpc_requester http://127.0.0.1:8000/signal
//
// Pair with: example_webrtc_rpc_responder
// Both sides must use the same signaling address and session ID.
//

#include <magpie/serializer/value.hpp>
#include "example_signaling.hpp"
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_rpc_requester.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <memory>
#include <thread>

int main(int argc, char** argv) {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ------------------------------------------------------------------
    // 1. Select signaling transport
    // ------------------------------------------------------------------
    const std::string address = argc > 1 ? argv[1] : "http://127.0.0.1:8000/signal";
    auto signaler = exampleSignaler(address, "magpie-cpp-rpc-demo", argc > 2 && std::string(argv[2]) == "--bind");

    // ------------------------------------------------------------------
    // 2. Create WebRTC connection and wait for peer
    // ------------------------------------------------------------------
    auto conn = std::make_shared<WebRtcConnection>(signaler);

    Logger::info("Waiting for peer (session: magpie-cpp-rpc-demo) ...");
    if (!conn->connect(30.0)) {
        Logger::error("No peer found within 30s — is the responder running?");
        return 1;
    }
    Logger::info("Connected! Calling service 'magpie/echo'.");

    // ------------------------------------------------------------------
    // 3. Create RPC requester and make calls
    // ------------------------------------------------------------------
    WebRtcRpcRequester req(conn, "magpie/echo");

    for (int i = 0; i < 5; ++i) {
        Value::Dict reqDict;
        reqDict["message"] = Value::fromString("ping #" + std::to_string(i));
        Value request = Value::fromDict(reqDict);

        Logger::info("Requester: calling 'magpie/echo' with request #" + std::to_string(i));

        try {
            Value response = req.call(request, /*timeoutSec=*/10.0);

            if (response.type() == Value::Type::Dict) {
                auto& d = response.asDict();
                auto it = d.find("message");
                if (it != d.end()) {
                    Logger::info("Requester: got response: '" + it->second.asString() + "'");
                }
            } else {
                Logger::info("Requester: got response type " +
                             std::to_string(static_cast<int>(response.type())));
            }
        } catch (const AckTimeoutError& e) {
            Logger::error("Requester: ACK timeout – " + std::string(e.what()));
        } catch (const ReplyTimeoutError& e) {
            Logger::error("Requester: Reply timeout – " + std::string(e.what()));
        } catch (const std::exception& e) {
            Logger::error("Requester: error – " + std::string(e.what()));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    req.close();
    conn->disconnect();
    return 0;
}
