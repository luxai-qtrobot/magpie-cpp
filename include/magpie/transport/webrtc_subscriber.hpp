#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <magpie/frames/frame.hpp>
#include <magpie/transport/stream_reader.hpp>
#include <magpie/transport/webrtc_connection.hpp>

namespace magpie {

class WebRtcSubscriber : public StreamReader {
public:
    static constexpr const char* VIDEO_TOPIC = "video";
    static constexpr const char* AUDIO_TOPIC = "audio";

    WebRtcSubscriber(std::shared_ptr<WebRtcConnection> connection,
                     const std::string&                topicFilter,
                     int                               queueSize = 10);

    ~WebRtcSubscriber() override;

protected:
    bool transportReadBlocking(std::unique_ptr<Frame>& outFrame,
                                std::string&            outTopic,
                                double                  timeoutSec) override;

    void transportClose() override;

private:
    void onDataMessage(const Value& payload, const std::string& topic);
    void onVideoFrame(const Value& frameDict);
    void onAudioFrame(const Value& frameDict);
    void enqueue(std::unique_ptr<Frame> frame, const std::string& topic);

    std::shared_ptr<WebRtcConnection> connection_;
    std::string                       topicFilter_;
    bool                              isVideoTopic_;
    bool                              isAudioTopic_;

    // For pub subscriptions (non-video/audio topics)
    WebRtcConnection::CallbackHandle  subHandle_{0};
    // For video/audio subscriptions
    WebRtcConnection::CallbackHandle  mediaHandle_{0};

    // Internal queue: (frame, topic)
    std::deque<std::pair<std::unique_ptr<Frame>, std::string>> dataQueue_;
    std::mutex              dataMutex_;
    std::condition_variable dataCv_;
    std::atomic<bool>       dataClosed_{false};
};

} // namespace magpie
