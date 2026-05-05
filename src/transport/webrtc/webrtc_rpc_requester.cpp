#include <magpie/transport/webrtc_rpc_requester.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/common.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

WebRtcRpcRequester::WebRtcRpcRequester(std::shared_ptr<WebRtcConnection> connection,
                                         const std::string&                serviceName,
                                         const std::string&                instanceName,
                                         double                            ackTimeoutSec,
                                         std::shared_ptr<BaseSchema>       schema)
    : RpcRequester(instanceName.empty() ? "WebRtcRpcRequester" : instanceName, std::move(schema))
    , connection_(std::move(connection))
    , serviceName_(serviceName)
    , ackTimeoutSec_(ackTimeoutSec)
{
    if (!connection_) {
        throw std::invalid_argument(name() + ": connection is null");
    }

    Logger::debug(name() + ": ready for service '" + serviceName_ + "'");
}

WebRtcRpcRequester::~WebRtcRpcRequester() {
    close();
}

// ---------------------------------------------------------------------------
// transportCall
// ---------------------------------------------------------------------------

WebRtcRpcRequester::Object
WebRtcRpcRequester::transportCall(const Object& request, double timeoutSec) {
    if (closing_.load()) {
        throw std::runtime_error(name() + ": transport is closed");
    }

    const std::string rid = getUniqueId();

    // Register pending call BEFORE sending (so a very fast reply is never missed)
    auto pending = std::make_shared<PendingCall>();
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_[rid] = pending;
    }

    // Register one-shot reply callback
    connection_->addRpcReplyCallback(rid, [this](const Value& msg) {
        this->onReplyMessage(msg);
    });

    // Build rpc_req envelope
    Value::Dict env;
    env["type"]    = Value::fromString("rpc_req");
    env["service"] = Value::fromString(serviceName_);
    env["rid"]     = Value::fromString(rid);
    env["payload"] = request;

    try {
        connection_->sendData(Value::fromDict(env));
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lk(pendingMtx_);
            pending_.erase(rid);
        }
        connection_->removeRpcReplyCallback(rid);
        throw std::runtime_error(name() + ": sendData failed: " + std::string(e.what()));
    }

    // ---- Wait for ACK ----
    double actualAckTimeout = ackTimeoutSec_;
    if (timeoutSec >= 0.0) {
        actualAckTimeout = std::min(timeoutSec, ackTimeoutSec_);
    }

    {
        std::unique_lock<std::mutex> lk(pending->mtx);
        if (actualAckTimeout >= 0.0) {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(actualAckTimeout));
            while (!pending->acked && pending->errorMsg.empty()) {
                if (pending->cvAck.wait_until(lk, deadline) == std::cv_status::timeout) {
                    break;
                }
            }
        } else {
            while (!pending->acked && pending->errorMsg.empty()) {
                pending->cvAck.wait(lk);
            }
        }

        if (!pending->acked) {
            {
                std::lock_guard<std::mutex> lk2(pendingMtx_);
                pending_.erase(rid);
            }
            connection_->removeRpcReplyCallback(rid);
            if (!pending->errorMsg.empty()) {
                throw std::runtime_error(pending->errorMsg);
            }
            throw AckTimeoutError(name() + ": no ACK from '" + serviceName_ +
                                  "' within " + std::to_string(actualAckTimeout) + "s");
        }
    }

    // ---- Wait for reply ----
    {
        std::unique_lock<std::mutex> lk(pending->mtx);
        if (timeoutSec >= 0.0) {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(timeoutSec));
            while (!pending->replied && pending->errorMsg.empty()) {
                if (pending->cvReply.wait_until(lk, deadline) == std::cv_status::timeout) {
                    break;
                }
            }
        } else {
            while (!pending->replied && pending->errorMsg.empty()) {
                pending->cvReply.wait(lk);
            }
        }

        if (!pending->replied) {
            {
                std::lock_guard<std::mutex> lk2(pendingMtx_);
                pending_.erase(rid);
            }
            connection_->removeRpcReplyCallback(rid);
            if (!pending->errorMsg.empty()) {
                throw std::runtime_error(pending->errorMsg);
            }
            throw ReplyTimeoutError(name() + ": no reply from '" + serviceName_ +
                                    "' within " + std::to_string(timeoutSec) + "s");
        }
    }

    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.erase(rid);
    }
    connection_->removeRpcReplyCallback(rid);
    return pending->replyPayload;
}

// ---------------------------------------------------------------------------
// onReplyMessage — called from WebRtcConnection routing (data channel thread)
// ---------------------------------------------------------------------------

void WebRtcRpcRequester::onReplyMessage(const Value& msg) {
    if (msg.type() != Value::Type::Dict) return;
    const auto& d = msg.asDict();

    auto itType = d.find("type");
    auto itRid  = d.find("rid");
    if (itType == d.end() || itRid == d.end()) return;
    if (itType->second.type() != Value::Type::String) return;
    if (itRid->second.type()  != Value::Type::String) return;

    const std::string msgType = itType->second.asString();
    const std::string rid     = itRid->second.asString();

    std::shared_ptr<PendingCall> pending;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        auto it = pending_.find(rid);
        if (it == pending_.end()) return;
        pending = it->second;
    }

    if (msgType == "rpc_ack") {
        {
            std::lock_guard<std::mutex> lk(pending->mtx);
            pending->acked = true;
        }
        pending->cvAck.notify_all();

    } else if (msgType == "rpc_rep") {
        auto itPayload = d.find("payload");
        {
            std::lock_guard<std::mutex> lk(pending->mtx);
            if (itPayload != d.end()) {
                pending->replyPayload = itPayload->second;
            } else {
                pending->errorMsg = name() + ": rpc_rep missing payload";
            }
            pending->replied = true;
        }
        pending->cvReply.notify_all();
    }
}

// ---------------------------------------------------------------------------
// transportClose
// ---------------------------------------------------------------------------

void WebRtcRpcRequester::transportClose() {
    Logger::debug(name() + ": closing");
    closing_.store(true);
    failAllPending(name() + ": transport closed");
}

void WebRtcRpcRequester::failAllPending(const std::string& err) {
    std::unordered_map<std::string, std::shared_ptr<PendingCall>> local;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        local.swap(pending_);
    }
    for (auto& kv : local) {
        connection_->removeRpcReplyCallback(kv.first);
        auto& p = kv.second;
        {
            std::lock_guard<std::mutex> lk(p->mtx);
            if (p->errorMsg.empty()) p->errorMsg = err;
        }
        p->cvAck.notify_all();
        p->cvReply.notify_all();
    }
}

} // namespace magpie
