//
// mqtt_rpc_responder.cpp
//
// MQTT RPC server.  Handles incoming requests from example_mqtt_rpc_requester
// by echoing the "message" field back in the response.
//
// Build:  cmake -DMAGPIE_WITH_MQTT=ON ..
// Run:    ./example_mqtt_rpc_responder
//
// Pair with: example_mqtt_rpc_requester
//

#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_rpc_responder.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <memory>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ------------------------------------------------------------------
    // 1. Connect
    // ------------------------------------------------------------------
    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect(10.0);

    // ------------------------------------------------------------------
    // 2. Create responder for service "magpie/echo"
    // ------------------------------------------------------------------
    MqttRpcResponder rsp(conn, "magpie/echo");

    Logger::info("MQTT RPC responder started. Handling service 'magpie/echo'.");

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

    while (true) {
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
    return 0;
}
