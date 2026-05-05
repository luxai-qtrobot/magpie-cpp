#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/serializer/value.hpp>

#include <iostream>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // Bind ROUTER responder (same as Python)
    ZmqRpcResponder server("tcp://*:5556");

    // Handler: echo request back
    auto onRequest = [](const Value& req) -> Value {
        Logger::info("on_request:\n" + req.toDebugString());
        return req;  // echo
    };

    while (true) {
        try {
            bool ok = server.handleOnce(onRequest, /*timeoutSec=*/3.0);   
            if (!ok) {
                Logger::info("responder: no request received.");
                continue;
            }
        }
        catch (const std::exception& e) {
            Logger::error(std::string("responder error: ") + e.what());
            break;
        }
    }

    Logger::info("stopping...");
    // server.close();  // optional, same as Python
    return 0;
}
