#include <magpie/transport/webrtc_rpc_responder.hpp>

#include <chrono>
#include <stdexcept>

#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

WebRtcRpcResponder::WebRtcRpcResponder(std::shared_ptr<WebRtcConnection> connection,
                                         const std::string&                serviceName,
                                         const std::string&                instanceName)
    : RpcResponder(instanceName.empty() ? "WebRtcRpcResponder" : instanceName)
    , connection_(std::move(connection))
    , serviceName_(serviceName)
{
    if (!connection_) {
        throw std::invalid_argument(name() + ": connection is null");
    }

    reqHandle_ = connection_->addRpcRequestCallback(
        serviceName_,
        [this](const Value& msg) {
            this->onRequestMessage(msg);
        });

    Logger::debug(name() + ": listening on service '" + serviceName_ + "'");
}

WebRtcRpcResponder::~WebRtcRpcResponder() {
    close();
}

// ---------------------------------------------------------------------------
// onRequestMessage — called from WebRtcConnection routing (data channel thread)
// ---------------------------------------------------------------------------

void WebRtcRpcResponder::onRequestMessage(const Value& msg) {
    if (reqClosed_.load()) return;
    {
        std::lock_guard<std::mutex> lk(reqMutex_);
        reqQueue_.push_back(msg);
    }
    reqCv_.notify_one();
}

// ---------------------------------------------------------------------------
// transportRecv
// ---------------------------------------------------------------------------

void WebRtcRpcResponder::transportRecv(Object&        outRequest,
                                         ClientContext& outClientCtx,
                                         double         timeoutSec) {
    if (reqClosed_.load()) {
        throw std::runtime_error(name() + ": transport is closed");
    }

    Value msg;
    {
        std::unique_lock<std::mutex> lk(reqMutex_);

        auto pred = [this]() { return !reqQueue_.empty() || reqClosed_.load(); };

        if (timeoutSec < 0.0) {
            reqCv_.wait(lk, pred);
        } else {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(timeoutSec));
            if (!reqCv_.wait_until(lk, deadline, pred)) {
                throw TimeoutError(name() + ": no request received within " +
                                   std::to_string(timeoutSec) + "s");
            }
        }

        if (reqClosed_.load() && reqQueue_.empty()) {
            throw std::runtime_error(name() + ": transport closed");
        }

        msg = std::move(reqQueue_.front());
        reqQueue_.pop_front();
    }

    // Validate rpc_req envelope
    if (msg.type() != Value::Type::Dict) {
        throw std::runtime_error(name() + ": malformed rpc_req: not a dict");
    }

    const auto& d = msg.asDict();
    auto itRid     = d.find("rid");
    auto itPayload = d.find("payload");

    if (itRid == d.end() || itRid->second.type() != Value::Type::String) {
        throw std::runtime_error(name() + ": rpc_req missing 'rid'");
    }
    if (itPayload == d.end()) {
        throw std::runtime_error(name() + ": rpc_req missing 'payload'");
    }

    const std::string rid = itRid->second.asString();

    // Send ACK immediately before invoking the handler
    try {
        Value::Dict ack;
        ack["type"] = Value::fromString("rpc_ack");
        ack["rid"]  = Value::fromString(rid);
        connection_->sendData(Value::fromDict(ack));
    } catch (const std::exception& e) {
        Logger::warning(name() + ": ACK send error for rid='" + rid + "': " + e.what());
        throw;
    }

    // Store client context (rid) for transportSend
    auto ctx  = std::make_shared<ClientCtxData>();
    ctx->rid  = rid;
    outClientCtx = std::static_pointer_cast<void>(ctx);

    // Return the request payload only (strip envelope)
    outRequest = itPayload->second;
}

// ---------------------------------------------------------------------------
// transportSend
// ---------------------------------------------------------------------------

void WebRtcRpcResponder::transportSend(const Object&        response,
                                         const ClientContext& clientCtx) {
    if (!connection_) {
        throw std::runtime_error(name() + ": connection is null");
    }
    if (!clientCtx) {
        throw std::runtime_error(name() + ": clientCtx is null");
    }

    auto ctx = std::static_pointer_cast<ClientCtxData>(clientCtx);

    Value::Dict rep;
    rep["type"]    = Value::fromString("rpc_rep");
    rep["rid"]     = Value::fromString(ctx->rid);
    rep["payload"] = response;

    try {
        connection_->sendData(Value::fromDict(rep));
    } catch (const std::exception& e) {
        Logger::warning(name() + ": reply send error for rid='" + ctx->rid + "': " + e.what());
        throw;
    }
}

// ---------------------------------------------------------------------------
// transportClose
// ---------------------------------------------------------------------------

void WebRtcRpcResponder::transportClose() {
    Logger::debug(name() + ": closing");

    reqClosed_.store(true);
    reqCv_.notify_all();

    if (connection_ && reqHandle_ != 0) {
        connection_->removeRpcRequestCallback(serviceName_, reqHandle_);
        reqHandle_ = 0;
    }
}

} // namespace magpie
