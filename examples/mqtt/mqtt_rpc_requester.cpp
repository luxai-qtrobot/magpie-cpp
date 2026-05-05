//
// mqtt_rpc_requester.cpp
//
// Performs RPC calls over MQTT.  Sends a Value dict request and prints
// the response received from example_mqtt_rpc_responder.
//
// Build:  cmake -DMAGPIE_WITH_MQTT=ON ..
// Run:    ./example_mqtt_rpc_requester
//
// Pair with: example_mqtt_rpc_responder
//

#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_rpc_requester.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <memory>
#include <thread>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ------------------------------------------------------------------
    // 1. Connect
    // ------------------------------------------------------------------
    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect(10.0);

    // ------------------------------------------------------------------
    // 2. Create requester for service "magpie/echo"
    // ------------------------------------------------------------------
    MqttRpcRequester req(conn, "magpie/echo");

    Logger::info("MQTT RPC requester started.");

    for (int i = 0; i < 5; ++i) {
        // Build a simple request dict
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
