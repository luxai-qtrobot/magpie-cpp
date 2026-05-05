#include <magpie/transport/rpc_responder.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

RpcResponder::RpcResponder(const std::string& name, std::shared_ptr<BaseSchema> schema)
    : name_{name}, schema_{std::move(schema)} {
}

bool RpcResponder::handleOnce(const RpcHandler& handler, double timeoutSec) {
    if (closed_)
        throw std::runtime_error(name_ + " is closed");
    if (!handler && !schema_)
        throw std::runtime_error(name_ + ": handler is required when no schema is set");

    Object        request;
    ClientContext ctx;

    try {
        transportRecv(request, ctx, timeoutSec);
    } catch (const TimeoutError&) {
        return false;
    } catch (const std::exception& e) {
        Logger::warning(name_ + ": transport receive failed: " + std::string(e.what()));
        throw;
    } catch (...) {
        Logger::warning(name_ + ": transport receive failed with unknown error");
        throw;
    }

    if (schema_) {
        Object response = schema_->dispatch(request);
        if (response.type() != Value::Type::Null) {
            try {
                transportSend(response, ctx);
            } catch (const std::exception& e) {
                Logger::warning(name_ + ": transport send failed: " + std::string(e.what()));
                throw;
            }
        }
    } else {
        Object response;
        try {
            response = handler(request);
        } catch (const std::exception& e) {
            Logger::warning(name_ + ": handler threw exception: " + std::string(e.what()));
            throw;
        }
        try {
            transportSend(response, ctx);
        } catch (const std::exception& e) {
            Logger::warning(name_ + ": transport send failed: " + std::string(e.what()));
            throw;
        }
    }

    return true;
}

void RpcResponder::close() {
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
