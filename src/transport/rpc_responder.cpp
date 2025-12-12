#include <magpie/transport/rpc_responder.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

RpcResponder::RpcResponder(const std::string& name)
    : name_{name} {
}

bool RpcResponder::handleOnce(const RpcHandler& handler, double timeoutSec) {
    if (closed_) {
        throw std::runtime_error(name_ + " is closed");
    }

    Object        request;
    ClientContext ctx;

    try {
        transportRecv(request, ctx, timeoutSec);
    } catch (const TimeoutError&) {
        // No request within timeout -> behave like Python: return False.
        return false;
    } catch (const std::exception& e) {
        Logger::warning(name_ + ": transport receive failed: " + std::string(e.what()));
        throw;
    } catch (...) {
        Logger::warning(name_ + ": transport receive failed with unknown error");
        throw;
    }

    // Call user handler
    Object response;
    try {
        response = handler(request);
    } catch (const std::exception& e) {
        Logger::warning(name_ + ": handler threw exception: " + std::string(e.what()));
        throw;
    } catch (...) {
        Logger::warning(name_ + ": handler threw unknown exception");
        throw;
    }

    // Send response
    try {
        transportSend(response, ctx);
    } catch (const std::exception& e) {
        Logger::warning(name_ + ": transport send failed: " + std::string(e.what()));
        throw;
    } catch (...) {
        Logger::warning(name_ + ": transport send failed with unknown error");
        throw;
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
