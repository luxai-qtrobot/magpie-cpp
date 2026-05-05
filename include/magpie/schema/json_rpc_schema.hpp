#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <magpie/schema/base_schema.hpp>
#include <magpie/serializer/value.hpp>

namespace magpie {

// Standard JSON-RPC 2.0 error codes
static constexpr int JSONRPC_PARSE_ERROR      = -32700;
static constexpr int JSONRPC_INVALID_REQUEST  = -32600;
static constexpr int JSONRPC_METHOD_NOT_FOUND = -32601;
static constexpr int JSONRPC_INVALID_PARAMS   = -32602;
static constexpr int JSONRPC_INTERNAL_ERROR   = -32603;


class JsonRpcError : public std::exception {
public:
    JsonRpcError(int code, const std::string& message)
        : code_{code}, message_{message} {}

    int                code()    const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }
    const char*        what()    const noexcept override { return message_.c_str(); }

private:
    int         code_;
    std::string message_;
};


/**
 * JsonRpcSchema
 *
 * JSON-RPC 2.0 dispatch layer for the responder side and envelope
 * builder/unwrapper for the requester side. Transport-agnostic.
 *
 * Responder usage:
 * @code
 *   auto schema = std::make_shared<JsonRpcSchema>();
 *   schema->register_method("add",
 *       [](const Value::Dict& p) -> Value {
 *           return Value::fromDouble(p.at("a").asDouble() + p.at("b").asDouble());
 *       }, "Add two numbers");
 *
 *   ZmqRpcResponder server("tcp://*:5556", nullptr, true, schema);
 *   while (true) {
 *       try { server.handleOnce(1.0); } catch (const TimeoutError&) {}
 *   }
 * @endcode
 *
 * Requester usage:
 * @code
 *   auto schema = JsonRpcSchema::from_json_file("api.json");
 *   ZmqRpcRequester client("tcp://127.0.0.1:5556", nullptr, {}, 2.0, schema);
 *   Value result = client.call("add", {{"a", Value::fromDouble(3)}, {"b", Value::fromDouble(4)}});
 * @endcode
 */
class JsonRpcSchema : public BaseSchema {
public:
    using Handler = std::function<Value(const Value::Dict&)>;

    JsonRpcSchema() = default;
    ~JsonRpcSchema() override = default;

    JsonRpcSchema(const JsonRpcSchema&)            = delete;
    JsonRpcSchema& operator=(const JsonRpcSchema&) = delete;

    // ------------------------------------------------------------------
    // Registration
    // ------------------------------------------------------------------

    /**
     * Register a method by name.
     *
     * @param name          Method name.
     * @param handler       Handler callable. May be nullptr on the requester
     *                      side or when attaching with set_handler() later.
     * @param description   Human-readable description.
     * @param input_schema  JSON Schema Value describing the parameters.
     * @param output_schema JSON Schema Value describing the return value.
     */
    virtual void register_method(
        const std::string& name,
        Handler            handler       = nullptr,
        const std::string& description   = "",
        const Value&       input_schema  = Value::null(),
        const Value&       output_schema = Value::null());

    /**
     * Attach an implementation to an already-registered method.
     * Mirrors Python @schema.handler("name").
     *
     * @throws std::out_of_range if name is not registered.
     */
    void set_handler(const std::string& name, Handler handler);

    // ------------------------------------------------------------------
    // Load from JSON
    // ------------------------------------------------------------------

    /**
     * Load schema from a JSON file.
     *
     * Expected format — a JSON array of method objects:
     * @code
     * [
     *   {
     *     "name": "add",
     *     "description": "Add two numbers",
     *     "inputSchema":  { "type": "object", "properties": { ... }, "required": [...] },
     *     "outputSchema": { "type": "number" }
     *   }
     * ]
     * @endcode
     * Methods are loaded without handlers. Attach with set_handler().
     */
    static std::shared_ptr<JsonRpcSchema> from_json_file(const std::string& path);

    /** Load schema from a JSON string. Same format as from_json_file(). */
    static std::shared_ptr<JsonRpcSchema> from_json_string(const std::string& s);

    // ------------------------------------------------------------------
    // Client-side helpers (used internally by RpcRequester::call)
    // ------------------------------------------------------------------

    /** Build a JSON-RPC 2.0 request envelope. Uses ULID for the id. */
    Value wrap(const std::string& method, const Value::Dict& params = {}) const override;

    /**
     * Extract the result from a JSON-RPC 2.0 response.
     * @throws JsonRpcError if the response contains an error object.
     */
    Value unwrap(const Value& response) const override;

    // ------------------------------------------------------------------
    // Dispatch (responder side)
    // ------------------------------------------------------------------

    /**
     * Dispatch a single request or a batch (Value::List).
     * Returns Value::null() for notifications (no id) and empty batches.
     */
    Value dispatch(const Value& request) override;

protected:
    struct MethodEntry {
        Handler     handler;
        std::string description;
        Value       input_schema;
        Value       output_schema;
    };

    std::unordered_map<std::string, MethodEntry> methods_;

    /** Used by McpSchema::from_json_file / from_json_string. */
    static void load_methods(JsonRpcSchema& schema, const std::string& json_text);

private:
    Value dispatch_single(const Value& req);
    Value make_error(const Value& id, int code, const std::string& msg) const;
    Value make_result(const Value& id, const Value& result) const;
};

} // namespace magpie
