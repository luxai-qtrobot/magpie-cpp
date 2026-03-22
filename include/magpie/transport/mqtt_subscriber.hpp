#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/serializer.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/stream_reader.hpp>

namespace magpie {

/**
 * MqttSubscriber
 *
 * Subscribes to an MQTT topic (or wildcard pattern) and yields Frame objects.
 * Extends StreamReader so frames are buffered in a background queue.
 *
 * Supports MQTT wildcards:
 *   '+'  – single-level wildcard  (e.g. "sensors/+/temp")
 *   '#'  – multi-level wildcard   (e.g. "sensors/#")
 *
 * @code
 * auto conn = std::make_shared<MqttConnection>("mqtt://localhost:1883");
 * conn->connect();
 *
 * MqttSubscriber sub(conn, "sensors/+");
 * std::unique_ptr<Frame> frame;
 * std::string topic;
 * if (sub.read(frame, topic, 5.0)) {
 *     // use frame
 * }
 * sub.close();
 * conn->disconnect();
 * @endcode
 */
class MqttSubscriber : public StreamReader {
public:
    /**
     * @param connection   Shared, already-connected MqttConnection.
     * @param topicFilter  MQTT topic or wildcard pattern to subscribe to.
     * @param serializer   Deserializer (defaults to MsgpackSerializer).
     * @param queueSize    StreamReader queue depth (>0 recommended for MQTT).
     * @param qos          Subscribe QoS override; -1 uses connection default.
     */
    explicit MqttSubscriber(std::shared_ptr<MqttConnection> connection,
                             const std::string&              topicFilter,
                             std::shared_ptr<Serializer>     serializer = nullptr,
                             int                             queueSize  = 10,
                             int                             qos        = -1);

    ~MqttSubscriber() override;

    MqttSubscriber(const MqttSubscriber&)            = delete;
    MqttSubscriber& operator=(const MqttSubscriber&) = delete;

protected:
    bool transportReadBlocking(std::unique_ptr<Frame>& outFrame,
                               std::string&            outTopic,
                               double                  timeoutSec) override;

    void transportClose() override;

private:
    void onMessage(const std::string& topic, const uint8_t* data, std::size_t size);

    std::shared_ptr<MqttConnection>     connection_;
    std::shared_ptr<Serializer>         serializer_;
    std::string                         topicFilter_;
    MqttConnection::SubscriptionHandle  subHandle_{0};

    // Internal queue: messages delivered by paho callback thread
    using RawItem = std::pair<std::string, std::vector<uint8_t>>;  // (topic, payload)
    std::mutex              mqttMutex_;
    std::condition_variable mqttCv_;
    std::deque<RawItem>     mqttQueue_;
    std::atomic<bool>       mqttClosed_{false};
};

} // namespace magpie
