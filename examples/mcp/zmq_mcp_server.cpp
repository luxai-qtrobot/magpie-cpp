/*
 * zmq_mcp_server.cpp — Robot side: serve MCP tools over MAGPIE/ZMQ
 *
 * Defines math tools using McpSchema and serves them over ZMQ.
 * An MCP client (e.g. Python zmq_mcp_client.py using FastMCP) can connect
 * to this server and call the tools via the MCP protocol.
 *
 * Run first:
 *   ./example_mcp_zmq_mcp_server
 *
 * Then connect a client (Python side):
 *   python examples/mcp/zmq_mcp_client.py
 */
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/schema/mcp_schema.hpp>
#include <magpie/utils/logger.hpp>

int main() {
    using namespace magpie;

    Logger::setLevel("INFO");

    auto schema = McpSchema::from_json_string(R"([
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
            "name": "subtract",
            "description": "Subtract b from a",
            "inputSchema": {
                "type": "object",
                "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
                "required": ["a", "b"]
            }
        },
        {
            "name": "multiply",
            "description": "Multiply two numbers",
            "inputSchema": {
                "type": "object",
                "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
                "required": ["a", "b"]
            }
        },
        {
            "name": "divide",
            "description": "Divide a by b. Raises an error if b is zero.",
            "inputSchema": {
                "type": "object",
                "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
                "required": ["a", "b"]
            }
        }
    ])", "math-robot", "1.0.0");

    schema->set_handler("add", [](const Value::Dict& p) -> Value {
        return Value::fromDouble(p.at("a").asDouble() + p.at("b").asDouble());
    });

    schema->set_handler("subtract", [](const Value::Dict& p) -> Value {
        return Value::fromDouble(p.at("a").asDouble() - p.at("b").asDouble());
    });

    schema->set_handler("multiply", [](const Value::Dict& p) -> Value {
        return Value::fromDouble(p.at("a").asDouble() * p.at("b").asDouble());
    });

    schema->set_handler("divide", [](const Value::Dict& p) -> Value {
        double b = p.at("b").asDouble();
        if (b == 0.0)
            throw std::runtime_error("Division by zero");
        return Value::fromDouble(p.at("a").asDouble() / b);
    });

    ZmqRpcResponder responder("tcp://*:5556", /*serializer=*/nullptr, /*bind=*/true, schema);
    Logger::info("Math robot MCP server running on tcp://*:5556. Press Ctrl-C to stop.");

    while (true) {
        try {
            responder.handleOnce(/*handler=*/nullptr, /*timeoutSec=*/1.0);
        } catch (const std::exception& e) {
            Logger::error(std::string("error: ") + e.what());
            break;
        }
    }

    responder.close();
    return 0;
}
