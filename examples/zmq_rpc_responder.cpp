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
            bool handled = server.handleOnce(onRequest, /*timeoutSec=*/1.0);
            (void)handled; // same as Python: ignore return value
        }
        catch (const TimeoutError&) {
            Logger::warning("zmq_responder example timeout on responding...");
        }
        catch (const std::exception& e) {
            Logger::error(std::string("responder error: ") + e.what());
            break;
        }
        catch (...) {
            Logger::error("responder unknown error");
            break;
        }
    }

    Logger::info("stopping...");
    // server.close();  // optional, same as Python
    return 0;
}
