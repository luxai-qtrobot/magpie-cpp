#include <magpie/transport/webrtc_stream_writer.hpp>

#include <magpie/frames/image_frame.hpp>
#include <magpie/frames/audio_frame.hpp>
#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

WebRtcStreamWriter::WebRtcStreamWriter(std::shared_ptr<WebRtcConnection> connection,
                                   std::shared_ptr<Serializer>       serializer,
                                   int                               queueSize)
    : StreamWriter("WebRtcStreamWriter", queueSize)
    , connection_(std::move(connection))
{
    if (!connection_) {
        throw std::invalid_argument("WebRtcStreamWriter: connection is null");
    }

    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    Logger::debug("WebRtcStreamWriter: created (queueSize=" + std::to_string(queueSize) + ")");
}

WebRtcStreamWriter::~WebRtcStreamWriter() {
    close();
}

void WebRtcStreamWriter::transportWrite(const Frame& frame, const std::string& topic) {
    if (!connection_) return;

    const bool useMedia = connection_->useMediaChannels();

    // Route image and audio frames through the appropriate channel
    if (const auto* img = dynamic_cast<const ImageFrameRaw*>(&frame)) {
        Frame::Dict dict;
        img->toDict(dict);
        const std::string effectiveTopic = topic.empty() ? "video" : topic;
        if (useMedia) {
            // magpie-media unreliable channel (default path)
            Value::Dict env;
            env["kind"]    = Value::fromString("video");
            env["topic"]   = Value::fromString(effectiveTopic);
            env["payload"] = Value::fromDict(dict);
            connection_->sendMediaFrame(Value::fromDict(env));
        } else {
            // Reliable magpie data channel (useMediaChannels=false)
            Value::Dict env;
            env["type"]    = Value::fromString("media");
            env["topic"]   = Value::fromString(effectiveTopic);
            env["payload"] = Value::fromDict(dict);
            connection_->sendData(Value::fromDict(env));
        }
        return;
    }

    if (const auto* aud = dynamic_cast<const AudioFrameRaw*>(&frame)) {
        Frame::Dict dict;
        aud->toDict(dict);
        const std::string effectiveTopic = topic.empty() ? "audio" : topic;
        if (useMedia) {
            Value::Dict env;
            env["kind"]    = Value::fromString("audio");
            env["topic"]   = Value::fromString(effectiveTopic);
            env["payload"] = Value::fromDict(dict);
            connection_->sendMediaFrame(Value::fromDict(env));
        } else {
            Value::Dict env;
            env["type"]    = Value::fromString("media");
            env["topic"]   = Value::fromString(effectiveTopic);
            env["payload"] = Value::fromDict(dict);
            connection_->sendData(Value::fromDict(env));
        }
        return;
    }

    if (topic.empty()) {
        Logger::warning("WebRtcStreamWriter: write() called without a topic — dropping.");
        return;
    }

    try {
        Frame::Dict dict;
        frame.toDict(dict);
        Value frameValue = Value::fromDict(dict);

        Value::Dict env;
        env["type"]    = Value::fromString("pub");
        env["topic"]   = Value::fromString(topic);
        env["payload"] = frameValue;

        connection_->sendData(Value::fromDict(env));
    } catch (const std::exception& e) {
        Logger::warning(std::string("WebRtcStreamWriter: write failed: ") + e.what());
    }
}

void WebRtcStreamWriter::transportClose() {
    // Connection is shared — closing the publisher does not disconnect.
    Logger::debug("WebRtcStreamWriter: closed (connection remains open).");
}

} // namespace magpie
