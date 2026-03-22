#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <magpie/serializer/serializer.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/rpc_requester.hpp>

namespace magpie {

/**
 * MqttRpcRequester
 *
 * Implements RPC-style request/response over MQTT pub/sub using the same
 * wire protocol as Python MqttRpcRequester.
 *
 * Protocol:
 *   Request  → <serviceName>/rpc/req
 *     payload: { "rid": "<ulid>", "reply_to": "<replyTopic>", "payload": <request> }
 *
 *   ACK      ← <replyTopic>
 *     payload: { "rid": "<ulid>", "ack": true }
 *
 *   Reply    ← <replyTopic>
 *     payload: { "rid": "<ulid>", "payload": <response> }
 *
 * @code
 * auto conn = std::make_shared<MqttConnection>("mqtt://localhost:1883");
 * conn->connect();
 *
 * MqttRpcRequester req(conn, "robot/motion");
 * Value result = req.call(Value::fromString("move"), 5.0);
 *
 * req.close();
 * conn->disconnect();
 * @endcode
 */
class MqttRpcRequester : public RpcRequester {
public:
    using Object = Value;

    /**
     * @param connection    Shared, already-connected MqttConnection.
     * @param serviceName   Base topic for the service (same as Python side).
     * @param serializer    Serializer (defaults to MsgpackSerializer).
     * @param instanceName  Optional human-readable name for logging.
     * @param ackTimeoutSec Timeout for the initial ACK (separate from total timeout).
     * @param qos           MQTT QoS for publish/subscribe; -1 uses connection default.
     */
    explicit MqttRpcRequester(std::shared_ptr<MqttConnection> connection,
                               const std::string&              serviceName,
                               std::shared_ptr<Serializer>     serializer    = nullptr,
                               const std::string&              instanceName  = std::string(),
                               double                          ackTimeoutSec = 2.0,
                               int                             qos           = -1);

    ~MqttRpcRequester() override;

    MqttRpcRequester(const MqttRpcRequester&)            = delete;
    MqttRpcRequester& operator=(const MqttRpcRequester&) = delete;

protected:
    Object transportCall(const Object& request, double timeoutSec) override;
    void   transportClose() override;

private:
    void onReplyMessage(const std::string& topic, const uint8_t* data, std::size_t size);

    // Per-call state
    struct PendingCall {
        std::mutex              mtx;
        std::condition_variable cvAck;
        std::condition_variable cvReply;
        bool        acked{false};
        bool        replied{false};
        Object      replyPayload;
        std::string errorMsg;
    };

    void failAllPending(const std::string& err);

    std::shared_ptr<MqttConnection> connection_;
    std::shared_ptr<Serializer>     serializer_;
    std::string                     serviceName_;
    std::string                     replyTopic_;
    double                          ackTimeoutSec_;
    int                             qos_;

    MqttConnection::SubscriptionHandle subHandle_{0};

    std::mutex                                           pendingMtx_;
    std::unordered_map<std::string,
                       std::shared_ptr<PendingCall>>     pending_;

    std::atomic<bool> closing_{false};
};

} // namespace magpie
