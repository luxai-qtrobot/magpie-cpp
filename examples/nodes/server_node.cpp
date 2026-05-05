#include <magpie/nodes/server_node.hpp>
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <thread>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // Hard-coded endpoint (same as Python example default)
    auto responder = std::make_shared<ZmqRpcResponder>("tcp://*:5556");

    // Handler: echo request back
    auto onRequest = [](const Value& req) -> Value {
        Logger::info("on_request:\n" + req.toDebugString());
        Value::Dict reply;
        reply["echo"] = req;
        return Value::fromDict(reply);
    };

    ServerNode server(
        responder,
        onRequest,
        /*maxWorkers=*/4, 
        /*pollTimeoutSec=*/0.01,
        /*paused=*/false,
        /*name=*/"MyServerNode"
    );

    // Explicit start (matches your final BaseNode decision)
    server.start();

    Logger::info("ServerNode started. Press Ctrl+C to stop.");

    try {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    } catch (...) {
        // fall through
    }

    Logger::info("Stopping server...");
    server.terminate();

    return 0;
}
