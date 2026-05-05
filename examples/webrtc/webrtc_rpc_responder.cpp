//
// webrtc_rpc_responder.cpp
//
// WebRTC RPC server.  Handles incoming requests from
// example_webrtc_rpc_requester by echoing the "message" field back
// in the response.
//
// Build:  cmake -DMAGPIE_WITH_WEBRTC=ON -DMAGPIE_WITH_MQTT=ON ..
// Run:    ./example_webrtc_rpc_responder
//
// Pair with: example_webrtc_rpc_requester
// Both sides must use the same broker URL and session_id.
//

#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_rpc_responder.hpp>
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
    auto conn = std::make_shared<WebRtcConnection>(signalConn, "magpie-cpp-rpc-demo");

    Logger::info("Waiting for peer (session: magpie-cpp-rpc-demo) ...");
    if (!conn->connect(30.0)) {
        Logger::error("No peer found within 30s — is the requester running?");
        signalConn->disconnect();
        return 1;
    }
    Logger::info("Connected! Handling service 'magpie/echo'.");

    // ------------------------------------------------------------------
    // 3. Create RPC responder and handle requests
    // ------------------------------------------------------------------
    WebRtcRpcResponder rsp(conn, "magpie/echo");

    // Echo handler: read "message" from request, put it back in response
    auto handler = [](const Value& req) -> Value {
        std::string msg = "(empty)";
        if (req.type() == Value::Type::Dict) {
            auto& d = req.asDict();
            auto it = d.find("message");
            if (it != d.end() && it->second.type() == Value::Type::String) {
                msg = it->second.asString();
            }
        }
        Logger::info("Responder: received '" + msg + "', echoing back");

        Value::Dict respDict;
        respDict["message"] = Value::fromString(msg);
        return Value::fromDict(respDict);
    };

    while (conn->isConnected()) {
        try {
            rsp.handleOnce(handler, /*timeoutSec=*/5.0);
        } catch (const TimeoutError&) {
            Logger::debug("Responder: waiting for requests...");
        } catch (const std::exception& e) {
            Logger::error("Responder: error – " + std::string(e.what()));
            break;
        }
    }

    rsp.close();
    conn->disconnect();
    signalConn->disconnect();
    return 0;
}
