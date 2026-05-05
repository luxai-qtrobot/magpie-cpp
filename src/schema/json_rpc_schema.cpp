#include <magpie/schema/json_rpc_schema.hpp>
#include <magpie/utils/common.hpp>
#include <magpie/utils/logger.hpp>

#include "json.hpp"

#include <fstream>
#include <stdexcept>

namespace magpie {

// ------------------------------------------------------------------
// JSON <-> Value helpers (file-private)
// ------------------------------------------------------------------

static Value json_to_value(const nlohmann::json& j) {
    if (j.is_null())             return Value::null();
    if (j.is_boolean())          return Value::fromBool(j.get<bool>());
    if (j.is_number_integer())   return Value::fromInt(j.get<std::int64_t>());
    if (j.is_number_float())     return Value::fromDouble(j.get<double>());
    if (j.is_string())           return Value::fromString(j.get<std::string>());
    if (j.is_array()) {
        Value::List list;
        list.reserve(j.size());
        for (const auto& item : j)
            list.push_back(json_to_value(item));
        return Value::fromList(list);
    }
    if (j.is_object()) {
        Value::Dict dict;
        for (auto it = j.items().begin(); it != j.items().end(); ++it)
            dict[it.key()] = json_to_value(it.value());
        return Value::fromDict(dict);
    }
    return Value::null();
}

static void parse_and_load(JsonRpcSchema& schema, const nlohmann::json& root) {
    if (!root.is_array())
        throw std::runtime_error("JsonRpcSchema: expected a JSON array of method objects");

    for (const auto& entry : root) {
        if (!entry.is_object() || !entry.contains("name"))
            throw std::runtime_error("JsonRpcSchema: each entry must be an object with a 'name' field");

        const std::string name        = entry.at("name").get<std::string>();
        const std::string description = entry.value("description", "");
        const Value input_schema  = entry.contains("inputSchema")
                                        ? json_to_value(entry["inputSchema"])
                                        : Value::null();
        const Value output_schema = entry.contains("outputSchema")
                                        ? json_to_value(entry["outputSchema"])
                                        : Value::null();

        schema.register_method(name, nullptr, description, input_schema, output_schema);
    }
}

// ------------------------------------------------------------------
// Registration
// ------------------------------------------------------------------

void JsonRpcSchema::register_method(
    const std::string& name,
    Handler            handler,
    const std::string& description,
    const Value&       input_schema,
    const Value&       output_schema)
{
    methods_[name] = MethodEntry{
        std::move(handler),
        description,
        input_schema.type() != Value::Type::Null ? input_schema
            : Value::fromDict({{"type", Value::fromString("object")}}),
        output_schema
    };
}

void JsonRpcSchema::set_handler(const std::string& name, Handler handler) {
    auto it = methods_.find(name);
    if (it == methods_.end())
        throw std::out_of_range("JsonRpcSchema: method '" + name + "' is not registered");
    it->second.handler = std::move(handler);
}

// ------------------------------------------------------------------
// Load from JSON
// ------------------------------------------------------------------

void JsonRpcSchema::load_methods(JsonRpcSchema& schema, const std::string& json_text) {
    parse_and_load(schema, nlohmann::json::parse(json_text));
}

std::shared_ptr<JsonRpcSchema> JsonRpcSchema::from_json_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("JsonRpcSchema: cannot open file: " + path);
    auto schema = std::make_shared<JsonRpcSchema>();
    parse_and_load(*schema, nlohmann::json::parse(f));
    return schema;
}

std::shared_ptr<JsonRpcSchema> JsonRpcSchema::from_json_string(const std::string& s) {
    auto schema = std::make_shared<JsonRpcSchema>();
    parse_and_load(*schema, nlohmann::json::parse(s));
    return schema;
}

// ------------------------------------------------------------------
// Client helpers
// ------------------------------------------------------------------

Value JsonRpcSchema::wrap(const std::string& method, const Value::Dict& params) const {
    Value::Dict req;
    req["jsonrpc"] = Value::fromString("2.0");
    req["method"]  = Value::fromString(method);
    req["id"]      = Value::fromString(getUniqueId());
    if (!params.empty())
        req["params"] = Value::fromDict(params);
    return Value::fromDict(req);
}

Value JsonRpcSchema::unwrap(const Value& response) const {
    if (response.type() != Value::Type::Dict)
        throw JsonRpcError(JSONRPC_INVALID_REQUEST, "Invalid response: not an object");

    const auto& d = response.asDict();

    auto err_it = d.find("error");
    if (err_it != d.end() && err_it->second.type() == Value::Type::Dict) {
        const auto& err = err_it->second.asDict();
        int code = JSONRPC_INTERNAL_ERROR;
        auto c = err.find("code");
        if (c != err.end()) code = static_cast<int>(c->second.asInt());
        std::string msg = "Unknown error";
        auto m = err.find("message");
        if (m != err.end()) msg = m->second.asString();
        throw JsonRpcError(code, msg);
    }

    auto res_it = d.find("result");
    if (res_it != d.end()) return res_it->second;
    return Value::null();
}

// ------------------------------------------------------------------
// Dispatch
// ------------------------------------------------------------------

Value JsonRpcSchema::make_error(const Value& id, int code, const std::string& msg) const {
    Value::Dict err;
    err["code"]    = Value::fromInt(code);
    err["message"] = Value::fromString(msg);
    Value::Dict resp;
    resp["jsonrpc"] = Value::fromString("2.0");
    resp["error"]   = Value::fromDict(err);
    resp["id"]      = id;
    return Value::fromDict(resp);
}

Value JsonRpcSchema::make_result(const Value& id, const Value& result) const {
    Value::Dict resp;
    resp["jsonrpc"] = Value::fromString("2.0");
    resp["result"]  = result;
    resp["id"]      = id;
    return Value::fromDict(resp);
}

Value JsonRpcSchema::dispatch_single(const Value& req) {
    if (req.type() != Value::Type::Dict)
        return make_error(Value::null(), JSONRPC_INVALID_REQUEST, "Request must be an object");

    const auto& d = req.asDict();

    // id is optional (notifications have no id)
    Value req_id = Value::null();
    auto id_it = d.find("id");
    if (id_it != d.end()) req_id = id_it->second;

    auto method_it = d.find("method");
    if (method_it == d.end() || method_it->second.type() != Value::Type::String)
        return make_error(req_id, JSONRPC_INVALID_REQUEST, "Missing or invalid 'method' field");

    const std::string& method_name = method_it->second.asString();

    auto entry_it = methods_.find(method_name);
    if (entry_it == methods_.end()) {
        if (req_id.type() == Value::Type::Null) return Value::null();
        return make_error(req_id, JSONRPC_METHOD_NOT_FOUND, "Method not found: " + method_name);
    }

    if (!entry_it->second.handler) {
        if (req_id.type() == Value::Type::Null) return Value::null();
        return make_error(req_id, JSONRPC_METHOD_NOT_FOUND, "Method not implemented: " + method_name);
    }

    Value::Dict params;
    auto params_it = d.find("params");
    if (params_it != d.end()) {
        if (params_it->second.type() == Value::Type::Dict) {
            params = params_it->second.asDict();
        } else {
            return make_error(req_id, JSONRPC_INVALID_PARAMS, "'params' must be an object");
        }
    }

    Value result;
    try {
        result = entry_it->second.handler(params);
    } catch (const std::exception& e) {
        Logger::warning(std::string("JsonRpcSchema: handler '") + method_name + "' raised: " + e.what());
        return make_error(req_id, JSONRPC_INTERNAL_ERROR, e.what());
    }

    if (req_id.type() == Value::Type::Null) return Value::null(); // notification
    return make_result(req_id, result);
}

Value JsonRpcSchema::dispatch(const Value& request) {
    if (request.type() == Value::Type::List) {
        Value::List responses;
        for (const auto& item : request.asList()) {
            Value r = dispatch_single(item);
            if (r.type() != Value::Type::Null)
                responses.push_back(std::move(r));
        }
        if (responses.empty()) return Value::null();
        return Value::fromList(responses);
    }
    return dispatch_single(request);
}

} // namespace magpie
