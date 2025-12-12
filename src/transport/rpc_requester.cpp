#include <magpie/transport/rpc_requester.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

RpcRequester::RpcRequester(const std::string& name)
    : name_{name} {
}

RpcRequester::Object RpcRequester::call(const Object& request, double timeoutSec) {
    if (closed_) {
        throw std::runtime_error(name_ + " is closed");
    }

    try {
        return transportCall(request, timeoutSec);
    } catch (const std::exception& e) {
        Logger::warning(name_ + ": RPC call failed: " + std::string(e.what()));
        throw;  // rethrow same exception
    } catch (...) {
        Logger::warning(name_ + ": RPC call failed with unknown error");
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
