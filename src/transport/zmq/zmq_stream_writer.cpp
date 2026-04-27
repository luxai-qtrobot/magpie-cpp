
#include <magpie/transport/zmq_stream_writer.hpp>
#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/utils/logger.hpp>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/value.hpp>

#include <zmq.h>
#include <cstring>

namespace magpie {

bool ZmqStreamWriter::startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

zmq_ctx_t* ZmqStreamWriter::sharedInprocContext() {
    // Shared context for inproc:// endpoints (similar to Python Context.instance()).
    static zmq_ctx_t* ctx = []() -> zmq_ctx_t* {
        void* raw = zmq_ctx_new();
        return static_cast<zmq_ctx_t*>(raw);
    }();
    return ctx;
}

ZmqStreamWriter::ZmqStreamWriter(const std::string& endpoint,                           
                           int queueSize,
                           bool bind,
                           const std::string& delivery,
                            std::shared_ptr<Serializer> serializer)
    : StreamWriter("ZmqStreamWriter", queueSize)
    , endpoint_{endpoint}
    , delivery_{delivery}    
{
    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    const bool inproc = startsWith(endpoint_, "inproc:");

    if (inproc) {
        context_    = sharedInprocContext();
        ownsContext_ = false;
    } else {
        context_    = static_cast<zmq_ctx_t*>(zmq_ctx_new());
        ownsContext_ = true;
    }

    if (!context_) {
        Logger::error("ZmqStreamWriter: failed to create ZeroMQ context");
        throw std::runtime_error("ZmqStreamWriter: zmq_ctx_new failed");
    }

    socket_ = zmq_socket(context_, ZMQ_PUB);
    if (!socket_) {
        Logger::error("ZmqStreamWriter: failed to create PUB socket");
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error("ZmqStreamWriter: zmq_socket failed");
    }

    // Delivery mode: keep only the latest message per subscriber.
    // Both must be set before bind/connect.
    if (delivery_ == "latest") {
        int hwm = 1;
        zmq_setsockopt(socket_, ZMQ_SNDHWM, &hwm, sizeof(hwm));
        int conflate = 1;
        zmq_setsockopt(socket_, ZMQ_CONFLATE, &conflate, sizeof(conflate));
    }

    int rc = 0;
    if (bind) {
        rc = zmq_bind(socket_, endpoint_.c_str());
    } else {
        rc = zmq_connect(socket_, endpoint_.c_str());
    }

    if (rc != 0) {
        Logger::error("ZmqStreamWriter: bind/connect failed for endpoint " + endpoint_);
        zmq_close(socket_);
        socket_ = nullptr;
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error("ZmqStreamWriter: bind/connect failed");
    }

    Logger::debug("ZmqStreamWriter is ready (" +
                  std::string(bind ? "bound" : "connected") +
                  " at " + endpoint_ +
                  ", delivery=" + delivery_ + ")");
}

ZmqStreamWriter::~ZmqStreamWriter() {
    close();
}

void ZmqStreamWriter::transportWrite(const Frame& frame,
                                  const std::string& topic) {
    if (!socket_) {
        Logger::warning("ZmqStreamWriter::transportWrite called with null socket");
        return;
    }

    try {
        const std::string actualTopic = topic.empty() ? std::string() : topic;
        // Frame -> Dict -> Value -> bytes
        std::vector<std::uint8_t> payload;
        if (serializer_) {
            Frame::Dict dict;
            frame.toDict(dict);
            Value value = Value::fromDict(dict);
            payload = serializer_->serialize(value);
        }

        // Topic part
        zmq_msg_t topicMsg;
        zmq_msg_init_size(&topicMsg, actualTopic.size());
        if (!actualTopic.empty()) {
            std::memcpy(zmq_msg_data(&topicMsg),
                        actualTopic.data(),
                        actualTopic.size());
        }

        // Payload part
        zmq_msg_t payloadMsg;
        zmq_msg_init_size(&payloadMsg, payload.size());
        if (!payload.empty()) {
            std::memcpy(zmq_msg_data(&payloadMsg),
                        payload.data(),
                        payload.size());
        }

        int rc = zmq_msg_send(&topicMsg, socket_, ZMQ_SNDMORE);
        zmq_msg_close(&topicMsg);

        if (rc == -1) {
            zmq_msg_close(&payloadMsg);
            Logger::warning("ZmqStreamWriter: failed to send topic part");
            return;
        }

        rc = zmq_msg_send(&payloadMsg, socket_, 0);
        zmq_msg_close(&payloadMsg);

        if (rc == -1) {
            Logger::warning("ZmqStreamWriter: failed to send payload part");
        }
    } catch (const std::exception& e) {
        Logger::warning(std::string("ZmqStreamWriter write failed with: ") + e.what());
    } catch (...) {
        Logger::warning("ZmqStreamWriter write failed with unknown exception");
    }
}

void ZmqStreamWriter::transportClose() {
    Logger::debug("ZmqStreamWriter is closing.");

    if (socket_) {
        if (zmq_close(socket_) != 0) {
            Logger::warning("ZmqStreamWriter socket close error");
        }
        socket_ = nullptr;
    }

    if (context_ && ownsContext_) {
        if (zmq_ctx_term(context_) != 0) {
            Logger::warning("ZmqStreamWriter context close error");
        }
        context_ = nullptr;
    }
}

} // namespace magpie
