#pragma once

#include <string>
#include <memory>

#include <magpie/schema/json_rpc_schema.hpp>

namespace magpie {

/**
 * McpSchema
 *
 * Extends JsonRpcSchema with built-in handlers for the MCP handshake and
 * tool-dispatch protocol (initialize, tools/list, tools/call).
 * Any method registered via register_method() is automatically exposed as an MCP tool.
 *
 * Responder usage:
 * @code
 *   auto schema = std::make_shared<McpSchema>("qtrobot", "1.0.0");
 *   schema->register_method("move_motor",
 *       [](const Value::Dict& p) -> Value {
 *           return Value::fromDict({{"success", Value::fromBool(true)}});
 *       },
 *       "Move a robot motor to a specific angle",
 *       input_schema);
 *
 *   MqttRpcResponder server(conn, "qtrobot", nullptr, {}, -1, schema);
 *   while (true) {
 *       try { server.handleOnce(1.0); } catch (const TimeoutError&) {}
 *   }
 * @endcode
 *
 * A Python FastMCP client using McpTransport sees standard MCP protocol.
 */
class McpSchema : public JsonRpcSchema {
public:
    explicit McpSchema(const std::string& name    = "magpie",
                       const std::string& version = "1.0.0");

    ~McpSchema() override = default;

    McpSchema(const McpSchema&)            = delete;
    McpSchema& operator=(const McpSchema&) = delete;

    // ------------------------------------------------------------------
    // Registration — overridden to track user methods as MCP tools
    // ------------------------------------------------------------------

    void register_method(
        const std::string& name,
        Handler            handler       = nullptr,
        const std::string& description   = "",
        const Value&       input_schema  = Value::null(),
        const Value&       output_schema = Value::null()) override;

    // ------------------------------------------------------------------
    // Load from JSON
    // ------------------------------------------------------------------

    /**
     * Load schema from a JSON file in MCP tools/list format.
     *
     * Accepts either a plain array or a dict with a "tools" key:
     * @code
     * [
     *   { "name": "add", "description": "...", "inputSchema": {...}, "outputSchema": {...} }
     * ]
     * @endcode
     * or:
     * @code
     * { "tools": [ ... ] }
     * @endcode
     */
    static std::shared_ptr<McpSchema> from_json_file(const std::string& path,
                                                      const std::string& name    = "magpie",
                                                      const std::string& version = "1.0.0");

    static std::shared_ptr<McpSchema> from_json_string(const std::string& s,
                                                        const std::string& name    = "magpie",
                                                        const std::string& version = "1.0.0");

private:
    struct ToolMeta {
        std::string description;
        Value       input_schema;
        Value       output_schema;
    };

    void register_builtins();

    Value mcp_initialize(const Value::Dict& params);
    Value mcp_tools_list(const Value::Dict& params);
    Value mcp_tools_call(const Value::Dict& params);

    std::string server_name_;
    std::string server_version_;
    std::unordered_map<std::string, ToolMeta> tools_;

    static const std::string MCP_PROTOCOL_VERSION;
};

} // namespace magpie
