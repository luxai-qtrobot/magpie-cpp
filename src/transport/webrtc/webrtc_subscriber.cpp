#include <magpie/transport/webrtc_subscriber.hpp>

#include <magpie/frames/image_frame.hpp>
#include <magpie/frames/audio_frame.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <stdexcept>

namespace magpie {

WebRtcSubscriber::WebRtcSubscriber(std::shared_ptr<WebRtcConnection> connection,
                                     const std::string&                topicFilter,
                                     int                               queueSize)
    : StreamReader("WebRtcSubscriber", queueSize)
    , connection_(std::move(connection))
    , topicFilter_(topicFilter)
    , isVideoTopic_(topicFilter == VIDEO_TOPIC)
    , isAudioTopic_(topicFilter == AUDIO_TOPIC)
{
    if (!connection_) {
        throw std::invalid_argument("WebRtcSubscriber: connection is null");
    }

    if (isVideoTopic_) {
        mediaHandle_ = connection_->addVideoCallback(
            [this](const Value& frameDict) {
                this->onVideoFrame(frameDict);
            });
    } else if (isAudioTopic_) {
        mediaHandle_ = connection_->addAudioCallback(
            [this](const Value& frameDict) {
                this->onAudioFrame(frameDict);
            });
    } else {
        subHandle_ = connection_->addPubCallback(
            topicFilter_,
            [this](const Value& payload, const std::string& topic) {
                this->onDataMessage(payload, topic);
            });
    }

    Logger::debug("WebRtcSubscriber: subscribed to '" + topicFilter_ +
                  "' (queueSize=" + std::to_string(queueSize) + ")");
}

WebRtcSubscriber::~WebRtcSubscriber() {
    close();
}

void WebRtcSubscriber::onVideoFrame(const Value& frameDict) {
    if (dataClosed_.load()) return;
    try {
        if (frameDict.type() != Value::Type::Dict) return;
        auto framePtr = Frame::fromDict(frameDict.asDict());
        if (!framePtr) return;
        enqueue(std::move(framePtr), topicFilter_);
    } catch (const std::exception& e) {
        Logger::warning(std::string("WebRtcSubscriber: video frame error: ") + e.what());
    }
}

void WebRtcSubscriber::onAudioFrame(const Value& frameDict) {
    if (dataClosed_.load()) return;
    try {
        if (frameDict.type() != Value::Type::Dict) return;
        auto framePtr = Frame::fromDict(frameDict.asDict());
        if (!framePtr) return;
        enqueue(std::move(framePtr), topicFilter_);
    } catch (const std::exception& e) {
        Logger::warning(std::string("WebRtcSubscriber: audio frame error: ") + e.what());
    }
}

void WebRtcSubscriber::onDataMessage(const Value& payload, const std::string& topic) {
    if (dataClosed_.load()) return;
    try {
        if (payload.type() != Value::Type::Dict) {
            Logger::warning("WebRtcSubscriber: payload is not a dict, dropping");
            return;
        }
        auto framePtr = Frame::fromDict(payload.asDict());
        if (!framePtr) {
            Logger::warning("WebRtcSubscriber: Frame::fromDict returned null, dropping");
            return;
        }
        enqueue(std::move(framePtr), topic);
    } catch (const std::exception& e) {
        Logger::warning(std::string("WebRtcSubscriber: frame reconstruction error: ") + e.what());
    }
}

void WebRtcSubscriber::enqueue(std::unique_ptr<Frame> frame, const std::string& topic) {
    {
        std::lock_guard<std::mutex> lk(dataMutex_);
        dataQueue_.emplace_back(std::move(frame), topic);
    }
    dataCv_.notify_one();
}

bool WebRtcSubscriber::transportReadBlocking(std::unique_ptr<Frame>& outFrame,
                                               std::string&            outTopic,
                                               double                  timeoutSec) {
    if (dataClosed_.load()) return false;

    std::pair<std::unique_ptr<Frame>, std::string> item;
    {
        std::unique_lock<std::mutex> lk(dataMutex_);

        auto pred = [this]() { return !dataQueue_.empty() || dataClosed_.load(); };

        if (timeoutSec < 0.0) {
            dataCv_.wait(lk, pred);
        } else {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(timeoutSec));
            if (!dataCv_.wait_until(lk, deadline, pred)) {
                throw TimeoutError(name() + ": no data received within " +
                                   std::to_string(timeoutSec) + "s");
            }
        }

        if (dataClosed_.load() && dataQueue_.empty()) return false;

        item = std::move(dataQueue_.front());
        dataQueue_.pop_front();
    }

    outFrame = std::move(item.first);
    outTopic = std::move(item.second);
    return true;
}

void WebRtcSubscriber::transportClose() {
    Logger::debug("WebRtcSubscriber: closing (topic='" + topicFilter_ + "')");

    dataClosed_.store(true);
    dataCv_.notify_all();

    if (connection_) {
        if (isVideoTopic_ && mediaHandle_ != 0) {
            connection_->removeVideoCallback(mediaHandle_);
            mediaHandle_ = 0;
        } else if (isAudioTopic_ && mediaHandle_ != 0) {
            connection_->removeAudioCallback(mediaHandle_);
            mediaHandle_ = 0;
        } else if (subHandle_ != 0) {
            connection_->removePubCallback(topicFilter_, subHandle_);
            subHandle_ = 0;
        }
    }
}

} // namespace magpie
