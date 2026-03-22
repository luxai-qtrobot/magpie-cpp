#include <magpie/transport/mqtt_rpc_responder.hpp>

#include <chrono>
#include <stdexcept>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MqttRpcResponder::MqttRpcResponder(std::shared_ptr<MqttConnection> connection,
                                     const std::string&              serviceName,
                                     std::shared_ptr<Serializer>     serializer,
                                     const std::string&              instanceName,
                                     int                             qos)
    : RpcResponder(instanceName.empty() ? "MqttRpcResponder" : instanceName)
    , connection_(std::move(connection))
    , serviceName_(serviceName)
    , qos_(qos)
{
    if (!connection_) {
        throw std::invalid_argument(name() + ": connection is null");
    }

    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    reqTopic_ = serviceName_ + "/rpc/req";

    subHandle_ = connection_->addSubscription(
        reqTopic_,
        [this](const std::string& topic, const uint8_t* data, std::size_t size) {
            this->onRequestMessage(topic, data, size);
        },
        qos_);

    Logger::debug(name() + ": listening on '" + reqTopic_ + "'");
}

MqttRpcResponder::~MqttRpcResponder() {
    close();
}

// ---------------------------------------------------------------------------
// onRequestMessage – called from paho network thread
// ---------------------------------------------------------------------------

void MqttRpcResponder::onRequestMessage(const std::string& topic,
                                          const uint8_t*     data,
                                          std::size_t        size) {
    if (reqClosed_.load()) return;

    std::vector<uint8_t> payload(data, data + size);
    {
        std::lock_guard<std::mutex> lk(reqMutex_);
        reqQueue_.emplace_back(topic, std::move(payload));
    }
    reqCv_.notify_one();
}

// ---------------------------------------------------------------------------
// transportRecv
// ---------------------------------------------------------------------------

void MqttRpcResponder::transportRecv(Object&        outRequest,
                                       ClientContext& outClientCtx,
                                       double         timeoutSec) {
    if (reqClosed_.load()) {
        throw std::runtime_error(name() + ": transport is closed");
    }

    // Wait for an incoming request
    RawItem item;
    {
        std::unique_lock<std::mutex> lk(reqMutex_);

        auto pred = [this]() {
            return !reqQueue_.empty() || reqClosed_.load();
        };

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

        item = std::move(reqQueue_.front());
        reqQueue_.pop_front();
    }

    // Deserialize request envelope
    Value envelope;
    try {
        envelope = serializer_->deserialize(item.second.data(), item.second.size());
    } catch (const std::exception& e) {
        throw std::runtime_error(name() + ": failed to deserialize request: " +
                                 std::string(e.what()));
    }

    if (envelope.type() != Value::Type::Dict) {
        throw std::runtime_error(name() + ": request envelope is not a dict");
    }

    const auto& d = envelope.asDict();

    auto itRid     = d.find("rid");
    auto itReplyTo = d.find("reply_to");
    auto itPayload = d.find("payload");

    if (itRid == d.end() || itRid->second.type() != Value::Type::String) {
        throw std::runtime_error(name() + ": request missing 'rid'");
    }
    if (itReplyTo == d.end() || itReplyTo->second.type() != Value::Type::String) {
        throw std::runtime_error(name() + ": request missing 'reply_to'");
    }
    if (itPayload == d.end()) {
        throw std::runtime_error(name() + ": request missing 'payload'");
    }

    const std::string rid     = itRid->second.asString();
    const std::string replyTo = itReplyTo->second.asString();

    // Send ACK immediately: {"rid": rid, "ack": true}
    try {
        Value::Dict ack;
        ack["rid"] = Value::fromString(rid);
        ack["ack"] = Value::fromBool(true);
        auto ackBytes = serializer_->serialize(Value::fromDict(ack));

        const int resolvedQos = (qos_ < 0) ? connection_->defaultPublishQos() : qos_;
        connection_->publish(replyTo, ackBytes.data(), ackBytes.size(), resolvedQos, false);
    } catch (const std::exception& e) {
        Logger::warning(name() + ": failed to send ack: " + std::string(e.what()));
        throw;
    }

    // Store client context (replyTo + rid) for transportSend
    auto ctx    = std::make_shared<ClientCtxData>();
    ctx->replyTo = replyTo;
    ctx->rid     = rid;
    outClientCtx = std::static_pointer_cast<void>(ctx);

    // Return request payload only (strip envelope)
    outRequest = itPayload->second;
}

// ---------------------------------------------------------------------------
// transportSend
// ---------------------------------------------------------------------------

void MqttRpcResponder::transportSend(const Object&        response,
                                       const ClientContext& clientCtx) {
    if (!connection_) {
        throw std::runtime_error(name() + ": connection is null");
    }
    if (!clientCtx) {
        throw std::runtime_error(name() + ": clientCtx is null");
    }

    auto ctx = std::static_pointer_cast<ClientCtxData>(clientCtx);

    // Build reply envelope: {"rid": rid, "payload": response}
    Value::Dict rep;
    rep["rid"]     = Value::fromString(ctx->rid);
    rep["payload"] = response;
    auto repBytes  = serializer_->serialize(Value::fromDict(rep));

    try {
        const int resolvedQos = (qos_ < 0) ? connection_->defaultPublishQos() : qos_;
        connection_->publish(ctx->replyTo, repBytes.data(), repBytes.size(), resolvedQos, false);
    } catch (const std::exception& e) {
        Logger::warning(name() + ": failed to send reply: " + std::string(e.what()));
        throw;
    }
}

// ---------------------------------------------------------------------------
// transportClose
// ---------------------------------------------------------------------------

void MqttRpcResponder::transportClose() {
    Logger::debug(name() + ": closing");

    reqClosed_.store(true);
    reqCv_.notify_all();

    if (connection_ && subHandle_ != 0) {
        connection_->removeSubscription(reqTopic_, subHandle_);
        subHandle_ = 0;
    }
}

} // namespace magpie
