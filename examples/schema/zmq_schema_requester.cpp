/*
 * zmq_schema_requester.cpp — ZMQ RPC requester with JsonRpcSchema
 *
 * Demonstrates schema-based call() on the requester side. The schema
 * handles JSON-RPC envelope wrap/unwrap automatically.
 *
 * Run zmq_schema_responder first.
 */
#include <magpie/transport/zmq_rpc_requester.hpp>
#include <magpie/schema/json_rpc_schema.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <thread>

int main() {
    using namespace magpie;

    Logger::setLevel("INFO");

    auto schema = JsonRpcSchema::from_json_string(R"([
        {"name": "add",     "description": "Add two numbers"},
        {"name": "sub",     "description": "Subtract b from a"},
        {"name": "mul",     "description": "Multiply two numbers"},
        {"name": "div",     "description": "Divide a by b"}
    ])");

    ZmqRpcRequester client(
        "tcp://127.0.0.1:5556",
        /*serializer=*/nullptr,
        /*identity=*/"",
        /*ackTimeoutSec=*/2.0,
        schema);

    try {
        Value result;

        result = client.call("add", {{"a", Value::fromDouble(3)}, {"b", Value::fromDouble(4)}});
        Logger::info("add:  3 + 4  = " + result.toDebugString());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        result = client.call("sub", {{"a", Value::fromDouble(10)}, {"b", Value::fromDouble(3)}});
        Logger::info("sub:  10 - 3 = " + result.toDebugString());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        result = client.call("mul", {{"a", Value::fromDouble(6)}, {"b", Value::fromDouble(7)}});
        Logger::info("mul:  6 * 7  = " + result.toDebugString());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        result = client.call("div", {{"a", Value::fromDouble(10)}, {"b", Value::fromDouble(4)}});
        Logger::info("div:  10 / 4 = " + result.toDebugString());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // ── error from server (division by zero) ──────────────────────────
        try {
            client.call("div", {{"a", Value::fromDouble(10)}, {"b", Value::fromDouble(0)}});
        } catch (const JsonRpcError& e) {
            Logger::warning("expected server error " + std::to_string(e.code()) + ": " + e.message());
        }

        // ── unknown method ────────────────────────────────────────────────
        try {
            client.call("unknown", {});
        } catch (const JsonRpcError& e) {
            Logger::warning("expected error " + std::to_string(e.code()) + ": " + e.message());
        }

    } catch (const std::exception& e) {
        Logger::error(std::string("error: ") + e.what());
    }

    client.close();
    Logger::info("Requester finished.");
    return 0;
}
