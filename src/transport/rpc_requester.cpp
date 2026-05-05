#include <magpie/transport/rpc_requester.hpp>
#include <magpie/schema/json_rpc_schema.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

RpcRequester::RpcRequester(const std::string& name, std::shared_ptr<BaseSchema> schema)
    : name_{name}, schema_{std::move(schema)} {
}

RpcRequester::Object RpcRequester::call(const Object& request, double timeoutSec) {
    if (closed_)
        throw std::runtime_error(name_ + " is closed");
    try {
        return transportCall(request, timeoutSec);
    } catch (const std::exception& e) {
        Logger::warning(name_ + ": RPC call failed: " + std::string(e.what()));
        throw;
    } catch (...) {
        Logger::warning(name_ + ": RPC call failed with unknown error");
        throw;
    }
}

RpcRequester::Object RpcRequester::call(const std::string& method,
                                         const Value::Dict& params,
                                         double             timeoutSec) {
    if (closed_)
        throw std::runtime_error(name_ + " is closed");
    if (!schema_)
        throw std::runtime_error(name_ + ": schema-based call requires a schema set at construction");
    try {
        Value envelope = schema_->wrap(method, params);
        Value response = transportCall(envelope, timeoutSec);
        return schema_->unwrap(response);
    } catch (const JsonRpcError&) {
        throw;
    } catch (const std::exception& e) {
        Logger::warning(name_ + ": RPC call '" + method + "' failed: " + std::string(e.what()));
        throw;
    }
}

void RpcRequester::close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    try {
        transportClose();
    } catch (const std::exception& e) {
        Logger::warning(name_ + " close: error closing transport: " + std::string(e.what()));
    } catch (...) {
        Logger::warning(name_ + " close: unknown error closing transport");
    }
}

} // namespace magpie
