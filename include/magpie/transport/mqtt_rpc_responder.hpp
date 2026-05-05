#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <magpie/serializer/serializer.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/rpc_responder.hpp>

namespace magpie {

/**
 * MqttRpcResponder
 *
 * Implements the server side of RPC-style request/response over MQTT.
 * Wire-protocol compatible with Python MqttRpcResponder.
 *
 * Protocol:
 *   Receives on <serviceName>/rpc/req:
 *     { "rid": "<ulid>", "reply_to": "<replyTopic>", "payload": <request> }
 *
 *   Sends ACK to <replyTopic>:
 *     { "rid": "<ulid>", "ack": true }
 *
 *   Sends reply to <replyTopic>:
 *     { "rid": "<ulid>", "payload": <response> }
 *
 * @code
 * auto conn = std::make_shared<MqttConnection>("mqtt://localhost:1883");
 * conn->connect();
 *
 * MqttRpcResponder rsp(conn, "robot/motion");
 * rsp.handleOnce([](const Value& req) { return Value::fromString("ok"); }, 10.0);
 *
 * rsp.close();
 * conn->disconnect();
 * @endcode
 */
class MqttRpcResponder : public RpcResponder {
public:
    using Object = Value;

    /**
     * @param connection   Shared, already-connected MqttConnection.
     * @param serviceName  Base topic for the service (same as Python side).
     * @param serializer   Serializer (defaults to MsgpackSerializer).
     * @param instanceName Optional human-readable name for logging.
     * @param qos          MQTT QoS for subscribe/publish; -1 uses connection default.
     */
    explicit MqttRpcResponder(std::shared_ptr<MqttConnection> connection,
                               const std::string&              serviceName,
                               std::shared_ptr<Serializer>     serializer   = nullptr,
                               const std::string&              instanceName = std::string(),
                               int                             qos          = -1,
                               std::shared_ptr<BaseSchema>     schema       = nullptr);

    ~MqttRpcResponder() override;

    MqttRpcResponder(const MqttRpcResponder&)            = delete;
    MqttRpcResponder& operator=(const MqttRpcResponder&) = delete;

protected:
    void transportRecv(Object& outRequest, ClientContext& outClientCtx, double timeoutSec) override;
    void transportSend(const Object& response, const ClientContext& clientCtx)             override;
    void transportClose()                                                                   override;

private:
    // Client context stored per request: holds the reply topic and request ID
    struct ClientCtxData {
        std::string replyTo;
        std::string rid;
    };

    void onRequestMessage(const std::string& topic, const uint8_t* data, std::size_t size);

    // Internal queue of raw incoming request envelopes
    using RawItem = std::pair<std::string, std::vector<uint8_t>>;  // (topic, payload)
    std::mutex              reqMutex_;
    std::condition_variable reqCv_;
    std::deque<RawItem>     reqQueue_;
    std::atomic<bool>       reqClosed_{false};

    std::shared_ptr<MqttConnection> connection_;
    std::shared_ptr<Serializer>     serializer_;
    std::string                     serviceName_;
    std::string                     reqTopic_;
    int                             qos_;

    MqttConnection::SubscriptionHandle subHandle_{0};
};

} // namespace magpie
