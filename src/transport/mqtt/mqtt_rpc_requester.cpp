#include <magpie/transport/mqtt_rpc_requester.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/common.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MqttRpcRequester::MqttRpcRequester(std::shared_ptr<MqttConnection> connection,
                                     const std::string&              serviceName,
                                     std::shared_ptr<Serializer>     serializer,
                                     const std::string&              instanceName,
                                     double                          ackTimeoutSec,
                                     int                             qos,
                                     std::shared_ptr<BaseSchema>     schema)
    : RpcRequester(instanceName.empty() ? "MqttRpcRequester" : instanceName, std::move(schema))
    , connection_(std::move(connection))
    , serviceName_(serviceName)
    , ackTimeoutSec_(ackTimeoutSec)
    , qos_(qos)
{
    if (!connection_) {
        throw std::invalid_argument(name() + ": connection is null");
    }

    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    // Each requester instance gets its own unique reply topic so multiple
    // requesters sharing the same connection don't cross-talk.
    replyTopic_ = "magpie/rpc/" + getUniqueId() + "/rep";

    subHandle_ = connection_->addSubscription(
        replyTopic_,
        [this](const std::string& topic, const uint8_t* data, std::size_t size) {
            this->onReplyMessage(topic, data, size);
        },
        qos_);

    Logger::debug(name() + ": created, reply_topic='" + replyTopic_ + "'");
}

MqttRpcRequester::~MqttRpcRequester() {
    close();
}

// ---------------------------------------------------------------------------
// transportCall
// ---------------------------------------------------------------------------

MqttRpcRequester::Object
MqttRpcRequester::transportCall(const Object& request, double timeoutSec) {
    if (closing_.load()) {
        throw std::runtime_error(name() + ": transport is closed");
    }

    const std::string rid = getUniqueId();

    // Build request envelope: {"rid": rid, "reply_to": replyTopic_, "payload": request}
    Value::Dict env;
    env["rid"]      = Value::fromString(rid);
    env["reply_to"] = Value::fromString(replyTopic_);
    env["payload"]  = request;
    auto bytes = serializer_->serialize(Value::fromDict(env));

    // Register pending call BEFORE publishing (so we don't miss a very fast reply)
    auto pending = std::make_shared<PendingCall>();
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_[rid] = pending;
    }

    // Publish request
    const int resolvedQos = (qos_ < 0) ? connection_->defaultPublishQos() : qos_;
    const std::string reqTopic = serviceName_ + "/rpc/req";
    try {
        connection_->publish(reqTopic, bytes.data(), bytes.size(), resolvedQos, false);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.erase(rid);
        throw std::runtime_error(name() + ": publish failed: " + std::string(e.what()));
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
            std::lock_guard<std::mutex> lk2(pendingMtx_);
            pending_.erase(rid);
            if (!pending->errorMsg.empty()) {
                throw std::runtime_error(pending->errorMsg);
            }
            throw AckTimeoutError(name() + ": no ack received within " +
                                  std::to_string(actualAckTimeout) + "s");
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
            std::lock_guard<std::mutex> lk2(pendingMtx_);
            pending_.erase(rid);
            if (!pending->errorMsg.empty()) {
                throw std::runtime_error(pending->errorMsg);
            }
            throw ReplyTimeoutError(name() + ": no reply received within " +
                                    std::to_string(timeoutSec) + "s");
        }
    }

    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.erase(rid);
    }
    return pending->replyPayload;
}

// ---------------------------------------------------------------------------
// onReplyMessage – called from paho network thread
// ---------------------------------------------------------------------------

void MqttRpcRequester::onReplyMessage(const std::string& /*topic*/,
                                        const uint8_t*     data,
                                        std::size_t        size) {
    Value obj;
    try {
        obj = serializer_->deserialize(data, size);
    } catch (const std::exception& e) {
        Logger::warning(name() + ": failed to deserialize reply: " + std::string(e.what()));
        return;
    }

    if (obj.type() != Value::Type::Dict) return;
    const auto& d = obj.asDict();

    auto itRid = d.find("rid");
    if (itRid == d.end() || itRid->second.type() != Value::Type::String) return;
    const std::string rid = itRid->second.asString();

    std::shared_ptr<PendingCall> pending;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        auto it = pending_.find(rid);
        if (it == pending_.end()) return;
        pending = it->second;
    }

    // ACK message: {"rid": rid, "ack": true}
    auto itAck = d.find("ack");
    if (itAck != d.end()) {
        bool ok = (itAck->second.type() == Value::Type::Bool) && itAck->second.asBool();
        {
            std::lock_guard<std::mutex> lk(pending->mtx);
            if (!ok) {
                pending->errorMsg = name() + ": invalid ack received";
            } else {
                pending->acked = true;
            }
        }
        pending->cvAck.notify_all();
        return;
    }

    // Reply message: {"rid": rid, "payload": ...}
    auto itPay = d.find("payload");
    if (itPay != d.end()) {
        {
            std::lock_guard<std::mutex> lk(pending->mtx);
            pending->replyPayload = itPay->second;
            pending->replied      = true;
        }
        pending->cvReply.notify_all();
        return;
    }

    // Unknown message format
    {
        std::lock_guard<std::mutex> lk(pending->mtx);
        pending->errorMsg = name() + ": invalid reply received";
    }
    pending->cvReply.notify_all();
}

// ---------------------------------------------------------------------------
// transportClose
// ---------------------------------------------------------------------------

void MqttRpcRequester::transportClose() {
    Logger::debug(name() + ": closing");
    closing_.store(true);

    // Remove subscription so we stop receiving replies
    if (connection_ && subHandle_ != 0) {
        connection_->removeSubscription(replyTopic_, subHandle_);
        subHandle_ = 0;
    }

    // Fail any pending calls so callers aren't blocked forever
    failAllPending(name() + ": transport closed");
}

void MqttRpcRequester::failAllPending(const std::string& err) {
    std::unordered_map<std::string, std::shared_ptr<PendingCall>> local;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        local.swap(pending_);
    }
    for (auto& kv : local) {
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
