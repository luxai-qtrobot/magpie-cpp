#include <magpie/transport/mqtt_publisher.hpp>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

MqttPublisher::MqttPublisher(std::shared_ptr<MqttConnection> connection,
                               std::shared_ptr<Serializer>     serializer,
                               int                             queueSize,
                               int                             qos,
                               int                             retain)
    : StreamWriter("MqttPublisher", queueSize)
    , connection_(std::move(connection))
    , qos_(qos)
    , retain_(retain >= 0 ? static_cast<bool>(retain)
                           : (connection_ ? connection_->defaultPublishRetain() : false))
{
    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    if (!connection_) {
        throw std::invalid_argument("MqttPublisher: connection is null");
    }

    Logger::debug("MqttPublisher: created (queueSize=" + std::to_string(queueSize) + ")");
}

MqttPublisher::~MqttPublisher() {
    close();
}

void MqttPublisher::transportWrite(const Frame& frame, const std::string& topic) {
    if (!connection_) {
        Logger::warning("MqttPublisher::transportWrite: connection is null");
        return;
    }

    try {
        // Frame → Dict → Value → bytes
        Frame::Dict dict;
        frame.toDict(dict);
        Value value = Value::fromDict(dict);
        auto  bytes = serializer_->serialize(value);

        const int  resolvedQos    = (qos_ < 0) ? connection_->defaultPublishQos() : qos_;
        const bool resolvedRetain = retain_ || connection_->defaultPublishRetain();

        connection_->publish(topic, bytes.data(), bytes.size(), resolvedQos, resolvedRetain);
    } catch (const std::exception& e) {
        Logger::warning(std::string("MqttPublisher: write failed: ") + e.what());
    } catch (...) {
        Logger::warning("MqttPublisher: write failed with unknown exception");
    }
}

void MqttPublisher::transportClose() {
    Logger::debug("MqttPublisher: closed (connection remains open)");
    // Do NOT disconnect the shared connection; other components may still use it.
}

} // namespace magpie
