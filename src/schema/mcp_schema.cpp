#include <magpie/schema/mcp_schema.hpp>
#include <magpie/utils/logger.hpp>

#include "json.hpp"

#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace magpie {

const std::string McpSchema::MCP_PROTOCOL_VERSION = "2024-11-05";

static const std::unordered_set<std::string> BUILTIN_METHODS = {
    "initialize",
    "notifications/initialized",
    "notifications/cancelled",
    "tools/list",
    "tools/call",
    "ping",
};

// ------------------------------------------------------------------
// JSON -> Value (duplicated from json_rpc_schema.cpp — private to this TU)
// ------------------------------------------------------------------

static Value json_to_value(const nlohmann::json& j) {
    if (j.is_null())             return Value::null();
    if (j.is_boolean())          return Value::fromBool(j.get<bool>());
    if (j.is_number_integer())   return Value::fromInt(j.get<std::int64_t>());
    if (j.is_number_float())     return Value::fromDouble(j.get<double>());
    if (j.is_string())           return Value::fromString(j.get<std::string>());
    if (j.is_array()) {
        Value::List list;
        for (const auto& item : j)
            list.push_back(json_to_value(item));
        return Value::fromList(list);
    }
    if (j.is_object()) {
        Value::Dict dict;
        for (const auto& kv : j.items())
            dict[kv.key()] = json_to_value(kv.value());
        return Value::fromDict(dict);
    }
    return Value::null();
}

// ------------------------------------------------------------------
// Constructor
// ------------------------------------------------------------------

McpSchema::McpSchema(const std::string& name, const std::string& version)
    : server_name_{name}, server_version_{version}
{
    register_builtins();
}

void McpSchema::register_builtins() {
    methods_["initialize"] = {
        [this](const Value::Dict& p) { return mcp_initialize(p); },
        "MCP initialize handshake",
        Value::fromDict({{"type", Value::fromString("object")}}),
        Value::null()
    };
    methods_["notifications/initialized"] = {
        [](const Value::Dict&) { return Value::null(); },
        "", Value::fromDict({{"type", Value::fromString("object")}}), Value::null()
    };
    methods_["notifications/cancelled"] = {
        [](const Value::Dict&) { return Value::null(); },
        "", Value::fromDict({{"type", Value::fromString("object")}}), Value::null()
    };
    methods_["ping"] = {
        [](const Value::Dict&) { return Value::fromDict({}); },
        "MCP ping",
        Value::fromDict({{"type", Value::fromString("object")}}),
        Value::null()
    };
    methods_["tools/list"] = {
        [this](const Value::Dict& p) { return mcp_tools_list(p); },
        "List available MCP tools",
        Value::fromDict({{"type", Value::fromString("object")}}),
        Value::null()
    };
    methods_["tools/call"] = {
        [this](const Value::Dict& p) { return mcp_tools_call(p); },
        "Call a tool by name",
        Value::fromDict({
            {"type", Value::fromString("object")},
            {"properties", Value::fromDict({
                {"name",      Value::fromDict({{"type", Value::fromString("string")}})},
                {"arguments", Value::fromDict({{"type", Value::fromString("object")}})}
            })},
            {"required", Value::fromList({Value::fromString("name")})}
        }),
        Value::null()
    };
}

// ------------------------------------------------------------------
// Registration
// ------------------------------------------------------------------

void McpSchema::register_method(
    const std::string& name,
    Handler            handler,
    const std::string& description,
    const Value&       input_schema,
    const Value&       output_schema)
{
    JsonRpcSchema::register_method(name, handler, description, input_schema, output_schema);

    if (BUILTIN_METHODS.count(name)) return;

    const auto& entry = methods_.at(name);
    tools_[name] = ToolMeta{
        entry.description,
        entry.input_schema,
        entry.output_schema
    };
}

// ------------------------------------------------------------------
// Load from JSON
// ------------------------------------------------------------------

static void load_mcp_methods(McpSchema& schema, const nlohmann::json& root) {
    const nlohmann::json* arr = &root;
    nlohmann::json unwrapped;
    if (root.is_object() && root.contains("tools")) {
        unwrapped = root.at("tools");
        arr = &unwrapped;
    }
    if (!arr->is_array())
        throw std::runtime_error("McpSchema: expected a JSON array of tool objects");

    for (const auto& entry : *arr) {
        if (!entry.is_object() || !entry.contains("name"))
            throw std::runtime_error("McpSchema: each tool entry must have a 'name' field");

        const std::string name        = entry.at("name").get<std::string>();
        const std::string description = entry.value("description", std::string{});
        const Value input_schema  = entry.contains("inputSchema")
                                        ? json_to_value(entry["inputSchema"])
                                        : Value::null();
        const Value output_schema = entry.contains("outputSchema")
                                        ? json_to_value(entry["outputSchema"])
                                        : Value::null();

        schema.register_method(name, nullptr, description, input_schema, output_schema);
    }
}

std::shared_ptr<McpSchema> McpSchema::from_json_file(
    const std::string& path,
    const std::string& name,
    const std::string& version)
{
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("McpSchema: cannot open file: " + path);
    auto schema = std::make_shared<McpSchema>(name, version);
    load_mcp_methods(*schema, nlohmann::json::parse(f));
    return schema;
}

std::shared_ptr<McpSchema> McpSchema::from_json_string(
    const std::string& s,
    const std::string& name,
    const std::string& version)
{
    auto schema = std::make_shared<McpSchema>(name, version);
    load_mcp_methods(*schema, nlohmann::json::parse(s));
    return schema;
}

// ------------------------------------------------------------------
// Built-in MCP handlers
// ------------------------------------------------------------------

Value McpSchema::mcp_initialize(const Value::Dict&) {
    return Value::fromDict({
        {"protocolVersion", Value::fromString(MCP_PROTOCOL_VERSION)},
        {"capabilities",    Value::fromDict({{"tools", Value::fromDict({})}})},
        {"serverInfo",      Value::fromDict({
            {"name",    Value::fromString(server_name_)},
            {"version", Value::fromString(server_version_)}
        })}
    });
}

Value McpSchema::mcp_tools_list(const Value::Dict&) {
    Value::List tools;
    for (const auto& kv : tools_) {
        const std::string& tool_name = kv.first;
        const ToolMeta&    meta      = kv.second;
        Value::Dict entry;
        entry["name"]        = Value::fromString(tool_name);
        entry["description"] = Value::fromString(meta.description);
        entry["inputSchema"] = meta.input_schema;
        if (meta.output_schema.type() != Value::Type::Null)
            entry["outputSchema"] = meta.output_schema;
        tools.push_back(Value::fromDict(entry));
    }
    return Value::fromDict({{"tools", Value::fromList(tools)}});
}

Value McpSchema::mcp_tools_call(const Value::Dict& params) {
    auto name_it = params.find("name");
    if (name_it == params.end() || name_it->second.type() != Value::Type::String)
        return Value::fromDict({
            {"content", Value::fromList({Value::fromDict({
                {"type", Value::fromString("text")},
                {"text", Value::fromString("'name' is required")}
            })})},
            {"isError", Value::fromBool(true)}
        });

    const std::string& tool_name = name_it->second.asString();

    auto tool_it = tools_.find(tool_name);
    auto meth_it = methods_.find(tool_name);
    if (tool_it == tools_.end() || meth_it == methods_.end() || !meth_it->second.handler) {
        return Value::fromDict({
            {"content", Value::fromList({Value::fromDict({
                {"type", Value::fromString("text")},
                {"text", Value::fromString("Unknown tool: " + tool_name)}
            })})},
            {"isError", Value::fromBool(true)}
        });
    }

    Value::Dict arguments;
    auto args_it = params.find("arguments");
    if (args_it != params.end() && args_it->second.type() == Value::Type::Dict)
        arguments = args_it->second.asDict();

    bool        is_error = false;
    std::string text;
    Value       result;
    try {
        result = meth_it->second.handler(arguments);
        switch (result.type()) {
            case Value::Type::Null:    text = "null"; break;
            case Value::Type::Bool:    text = result.asBool() ? "true" : "false"; break;
            case Value::Type::Int:     text = std::to_string(result.asInt()); break;
            case Value::Type::Double:  text = std::to_string(result.asDouble()); break;
            case Value::Type::String:  text = result.asString(); break;
            default:                   text = result.toDebugString(); break;
        }
    } catch (const std::exception& e) {
        is_error = true;
        text     = e.what();
    }

    Value::Dict response;
    response["content"] = Value::fromList({Value::fromDict({
        {"type", Value::fromString("text")},
        {"text", Value::fromString(text)}
    })});
    response["isError"] = Value::fromBool(is_error);

    if (!is_error
        && result.type() == Value::Type::Dict
        && tool_it->second.output_schema.type() != Value::Type::Null)
    {
        response["structuredContent"] = result;
    }

    return Value::fromDict(response);
}

} // namespace magpie
