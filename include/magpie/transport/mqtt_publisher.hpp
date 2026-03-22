#pragma once

#include <memory>
#include <string>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/serializer.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/stream_writer.hpp>

namespace magpie {

/**
 * MqttPublisher
 *
 * Publishes Frame objects to an MQTT broker via a shared MqttConnection.
 * Extends StreamWriter so frames can be queued and sent from a background thread.
 *
 * @code
 * auto conn = std::make_shared<MqttConnection>("mqtt://localhost:1883");
 * conn->connect();
 *
 * MqttPublisher pub(conn, nullptr, 10);
 * StringFrame f("hello");
 * pub.write(f, "sensors/temperature");
 *
 * pub.close();
 * conn->disconnect();
 * @endcode
 */
class MqttPublisher : public StreamWriter {
public:
    /**
     * @param connection   Shared, already-connected MqttConnection.
     * @param serializer   Serializer for frame encoding (defaults to MsgpackSerializer).
     * @param queueSize    StreamWriter queue depth (0 = direct/synchronous write).
     * @param qos          MQTT QoS override; -1 uses connection default.
     * @param retain       MQTT retain flag override; uses connection default if not set.
     */
    explicit MqttPublisher(std::shared_ptr<MqttConnection> connection,
                           std::shared_ptr<Serializer>     serializer = nullptr,
                           int                             queueSize  = 10,
                           int                             qos        = -1,
                           int                             retain     = -1);

    ~MqttPublisher() override;

    MqttPublisher(const MqttPublisher&)            = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

protected:
    void transportWrite(const Frame& frame, const std::string& topic) override;
    void transportClose() override;

private:
    std::shared_ptr<MqttConnection> connection_;
    std::shared_ptr<Serializer>     serializer_;
    int                             qos_;
    bool                            retain_;
};

} // namespace magpie
