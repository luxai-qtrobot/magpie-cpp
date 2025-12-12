#include <magpie/transport/zmq_rpc_requester.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/utils/logger.hpp>

#include <iostream>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // Connect to Python ZMQRpcResponder
    ZmqRpcRequester client("tcp://127.0.0.1:5556");

    // Build request object (Python-side expects a dict/object)
    Value::Dict request;
    request["message"] = Value::fromString("Hello from C++");
    request["count"]   = Value::fromInt(42);

    try {
        Logger::info("Sending RPC request...");
        Value response = client.call(Value::fromDict(request), /*timeoutSec=*/5.0);

        // Expect response to be a dict or value returned by Python handler
        if (response.type() == Value::Type::Dict) {
            Logger::info("Received response dict:");
            for (const auto& kv : response.asDict()) {
                Logger::info("  " + kv.first + " = " + kv.second.toDebugString());
            }
        } else {
            Logger::info("Received response: " + response.toDebugString());
        }
    } catch (const TimeoutError& e) {
        Logger::error(std::string("RPC timeout: ") + e.what());
    } catch (const std::exception& e) {
        Logger::error(std::string("RPC error: ") + e.what());
    }

    client.close();
    Logger::info("Requester finished.");
    return 0;
}
