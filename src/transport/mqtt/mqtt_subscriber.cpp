#include <magpie/transport/mqtt_subscriber.hpp>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>

namespace magpie {

MqttSubscriber::MqttSubscriber(std::shared_ptr<MqttConnection> connection,
                                 const std::string&              topicFilter,
                                 std::shared_ptr<Serializer>     serializer,
                                 int                             queueSize,
                                 int                             qos)
    : StreamReader("MqttSubscriber", queueSize)
    , connection_(std::move(connection))
    , topicFilter_(topicFilter)
{
    if (!connection_) {
        throw std::invalid_argument("MqttSubscriber: connection is null");
    }

    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    // Register our callback with the shared connection
    subHandle_ = connection_->addSubscription(
        topicFilter_,
        [this](const std::string& topic, const uint8_t* data, std::size_t size) {
            this->onMessage(topic, data, size);
        },
        qos);

    Logger::debug("MqttSubscriber: subscribed to '" + topicFilter_ +
                  "' (queueSize=" + std::to_string(queueSize) + ")");
}

MqttSubscriber::~MqttSubscriber() {
    close();
}

void MqttSubscriber::onMessage(const std::string& topic, const uint8_t* data, std::size_t size) {
    if (mqttClosed_.load()) return;

    std::vector<uint8_t> payload(data, data + size);
    {
        std::lock_guard<std::mutex> lk(mqttMutex_);
        mqttQueue_.emplace_back(topic, std::move(payload));
    }
    mqttCv_.notify_one();
}

bool MqttSubscriber::transportReadBlocking(std::unique_ptr<Frame>& outFrame,
                                             std::string&            outTopic,
                                             double                  timeoutSec) {
    if (mqttClosed_.load()) {
        return false;
    }

    RawItem item;
    {
        std::unique_lock<std::mutex> lk(mqttMutex_);

        auto pred = [this]() {
            return !mqttQueue_.empty() || mqttClosed_.load();
        };

        if (timeoutSec < 0.0) {
            mqttCv_.wait(lk, pred);
        } else {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(timeoutSec));
            if (!mqttCv_.wait_until(lk, deadline, pred)) {
                throw TimeoutError(name() + ": no data received within " +
                                   std::to_string(timeoutSec) + "s");
            }
        }

        if (mqttClosed_.load() && mqttQueue_.empty()) {
            return false;
        }

        item = std::move(mqttQueue_.front());
        mqttQueue_.pop_front();
    }

    // Deserialize payload → Value → Frame
    try {
        Value value = serializer_->deserialize(item.second.data(), item.second.size());

        if (value.type() != Value::Type::Dict) {
            Logger::warning(name() + ": deserialized value is not a dict, dropping");
            return false;
        }

        auto framePtr = Frame::fromDict(value.asDict());
        if (!framePtr) {
            Logger::warning(name() + ": Frame::fromDict returned null, dropping");
            return false;
        }

        outFrame = std::move(framePtr);
        outTopic = std::move(item.first);
        return true;
    } catch (const std::exception& e) {
        Logger::warning(name() + ": deserialization error: " + std::string(e.what()));
        return false;
    }
}

void MqttSubscriber::transportClose() {
    Logger::debug("MqttSubscriber: closing (topic='" + topicFilter_ + "')");

    mqttClosed_.store(true);
    mqttCv_.notify_all();

    if (connection_ && subHandle_ != 0) {
        connection_->removeSubscription(topicFilter_, subHandle_);
        subHandle_ = 0;
    }
}

} // namespace magpie
