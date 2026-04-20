#include <magpie/transport/mqtt_stream_writer.hpp>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

MqttStreamWriter::MqttStreamWriter(std::shared_ptr<MqttConnection> connection,
                               std::shared_ptr<Serializer>     serializer,
                               int                             queueSize,
                               int                             qos,
                               int                             retain)
    : StreamWriter("MqttStreamWriter", queueSize)
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
        throw std::invalid_argument("MqttStreamWriter: connection is null");
    }

    Logger::debug("MqttStreamWriter: created (queueSize=" + std::to_string(queueSize) + ")");
}

MqttStreamWriter::~MqttStreamWriter() {
    close();
}

void MqttStreamWriter::transportWrite(const Frame& frame, const std::string& topic) {
    if (!connection_) {
        Logger::warning("MqttStreamWriter::transportWrite: connection is null");
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
        Logger::warning(std::string("MqttStreamWriter: write failed: ") + e.what());
    } catch (...) {
        Logger::warning("MqttStreamWriter: write failed with unknown exception");
    }
}

void MqttStreamWriter::transportClose() {
    Logger::debug("MqttStreamWriter: closed (connection remains open)");
    // Do NOT disconnect the shared connection; other components may still use it.
}

} // namespace magpie
