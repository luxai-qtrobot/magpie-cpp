/*
 * zmq_schema_responder.cpp — ZMQ RPC responder with JsonRpcSchema
 *
 * Demonstrates two ways to define and attach handlers:
 *
 *   Way A — load API from JSON string, attach handlers with set_handler()
 *   Way B — register_method() with handler inline
 *
 * Run this first, then run zmq_schema_requester.
 */
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/schema/json_rpc_schema.hpp>
#include <magpie/utils/logger.hpp>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // ── Way A: load schema from JSON, attach handlers with set_handler() ──

    auto schema = JsonRpcSchema::from_json_string(R"([
        {
            "name": "add",
            "description": "Add two numbers",
            "inputSchema": {
                "type": "object",
                "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
                "required": ["a", "b"]
            }
        },
        {
            "name": "sub",
            "description": "Subtract b from a",
            "inputSchema": {
                "type": "object",
                "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
                "required": ["a", "b"]
            }
        }
    ])");

    schema->set_handler("add", [](const Value::Dict& p) -> Value {
        return Value::fromDouble(p.at("a").asDouble() + p.at("b").asDouble());
    });

    schema->set_handler("sub", [](const Value::Dict& p) -> Value {
        return Value::fromDouble(p.at("a").asDouble() - p.at("b").asDouble());
    });

    // ── Way B: register_method() with inline handler ──────────────────────

    schema->register_method("mul",
        [](const Value::Dict& p) -> Value {
            return Value::fromDouble(p.at("a").asDouble() * p.at("b").asDouble());
        },
        "Multiply two numbers");

    schema->register_method("div",
        [](const Value::Dict& p) -> Value {
            double b = p.at("b").asDouble();
            if (b == 0.0)
                throw JsonRpcError(JSONRPC_INVALID_PARAMS, "division by zero");
            return Value::fromDouble(p.at("a").asDouble() / b);
        },
        "Divide a by b");

    ZmqRpcResponder server("tcp://*:5556", /*serializer=*/nullptr, /*bind=*/true, schema);
    Logger::info("zmq_schema_responder: listening on tcp://*:5556");

    while (true) {
        try {
            server.handleOnce(/*handler=*/nullptr, /*timeoutSec=*/1.0);
        } catch (const std::exception& e) {
            Logger::error(std::string("error: ") + e.what());
            break;
        }
    }

    server.close();
    return 0;
}
