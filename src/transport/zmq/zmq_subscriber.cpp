#include <magpie/transport/zmq_subscriber.hpp>
#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/utils/logger.hpp>

#include <zmq.h>
#include <chrono>
#include <cstring>

namespace magpie {

bool ZmqSubscriber::startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

ZmqSubscriber::ZmqSubscriber(const std::string& endpoint,
                             const std::vector<std::string>& topics,                             
                             int queueSize,
                             bool bind,
                             const std::string& delivery,
                            std::shared_ptr<Serializer> serializer)
    : StreamReader("ZmqSubscriber", queueSize)
    , endpoint_{endpoint}
    , topics_{topics}
    , delivery_{delivery}
{
    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    // Normalize topics: empty list -> subscribe to all ("" topic)
    if (topics_.empty()) {
        topics_.push_back(std::string());
    }

    const bool inproc = startsWith(endpoint_, "inproc:");

    if (inproc) {
        // Use shared context for inproc (like Context.instance() in Python)
        extern zmq_ctx_t* ZmqPublisher_sharedInprocContext(); // we could expose, but to keep it simple:
        // For now, just use a private per-subscriber context even for inproc:
        context_     = static_cast<zmq_ctx_t*>(zmq_ctx_new());
        ownsContext_ = true;
    } else {
        context_     = static_cast<zmq_ctx_t*>(zmq_ctx_new());
        ownsContext_ = true;
    }

    if (!context_) {
        Logger::error("ZmqSubscriber: failed to create ZeroMQ context");
        throw std::runtime_error("ZmqSubscriber: zmq_ctx_new failed");
    }

    socket_ = zmq_socket(context_, ZMQ_SUB);
    if (!socket_) {
        Logger::error("ZmqSubscriber: failed to create SUB socket");
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error("ZmqSubscriber: zmq_socket failed");
    }

    if (delivery_ == "latest") {
        int hwm = 1;
        zmq_setsockopt(socket_, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    }

    int rc = 0;
    if (bind) {
        rc = zmq_bind(socket_, endpoint_.c_str());
    } else {
        rc = zmq_connect(socket_, endpoint_.c_str());
    }

    if (rc != 0) {
        Logger::error("ZmqSubscriber: bind/connect failed for endpoint " + endpoint_);
        zmq_close(socket_);
        socket_ = nullptr;
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error("ZmqSubscriber: bind/connect failed");
    }

    // Subscription setup
    bool subscribeAll = false;
    for (const auto& t : topics_) {
        if (t.empty()) {
            subscribeAll = true;
            break;
        }
    }

    if (subscribeAll) {
        const char emptyTopic[] = "";
        zmq_setsockopt(socket_, ZMQ_SUBSCRIBE, emptyTopic, 0);
    } else {
        for (const auto& t : topics_) {
            zmq_setsockopt(socket_,
                           ZMQ_SUBSCRIBE,
                           t.data(),
                           static_cast<int>(t.size()));
        }
    }

    Logger::debug("ZmqSubscriber is ready (" +
                  std::string(bind ? "bound" : "connected") +
                  " at " + endpoint_ +
                  " for topics: " + std::to_string(topics_.size()) +
                  ", delivery=" + delivery_ +
                  ", queue_size=" + std::to_string(queueSize) + ")");
}

ZmqSubscriber::ZmqSubscriber(const std::string& endpoint,
                             const std::string& topic,                             
                             int queueSize,
                             bool bind,
                             const std::string& delivery,
                            std::shared_ptr<Serializer> serializer)
    : ZmqSubscriber(endpoint,
                    std::vector<std::string>{topic},                    
                    queueSize,
                    bind,
                    delivery,
                    std::move(serializer))
{
}

ZmqSubscriber::~ZmqSubscriber() {
    close();
}

bool ZmqSubscriber::transportReadBlocking(std::unique_ptr<Frame>& outFrame,
                                          std::string& outTopic,
                                          double timeoutSec) {
    if (!socket_ || !context_) {
        // Logger::debug("ZmqSubscriber: socket/context null, stop reading.");
        return false;
    }

    using clock = std::chrono::steady_clock;
    auto start  = clock::now();

    zmq_pollitem_t items[1];
    items[0].socket  = socket_;
    items[0].fd      = 0;
    items[0].events  = ZMQ_POLLIN;
    items[0].revents = 0;

    while (true) {
        long pollMs = 1000; // chunked polling like Python version

        if (timeoutSec >= 0.0) {
            auto elapsed   = std::chrono::duration_cast<std::chrono::duration<double>>(clock::now() - start).count();
            double remain  = timeoutSec - elapsed;
            if (remain <= 0.0) {
                throw TimeoutError(name() + ": no data received within " + std::to_string(timeoutSec) + " seconds");
            }
            pollMs = static_cast<long>(remain * 1000.0);
            if (pollMs > 1000) pollMs = 1000;
            if (pollMs < 1)   pollMs = 1;
        }

        int rc = zmq_poll(items, 1, pollMs);
        if (rc == -1) {
            // Poll error – treat as transport error; user can decide what to do.
            Logger::warning(name() + ": transport error during recv (zmq_poll)");
            throw std::runtime_error(name() + ": zmq_poll failed");
        }

        if (items[0].revents & ZMQ_POLLIN) {
            // Receive topic and message
            zmq_msg_t topicMsg;
            zmq_msg_init(&topicMsg);
            rc = zmq_msg_recv(&topicMsg, socket_, 0);
            if (rc == -1) {
                zmq_msg_close(&topicMsg);
                Logger::warning(name() + ": failed to receive topic");
                continue;
            }

            zmq_msg_t payloadMsg;
            zmq_msg_init(&payloadMsg);
            rc = zmq_msg_recv(&payloadMsg, socket_, 0);
            if (rc == -1) {
                zmq_msg_close(&topicMsg);
                zmq_msg_close(&payloadMsg);
                Logger::warning(name() + ": failed to receive payload");
                continue;
            }

            // Build topic string
            outTopic.assign(static_cast<const char*>(zmq_msg_data(&topicMsg)),
                            zmq_msg_size(&topicMsg));
            zmq_msg_close(&topicMsg);

            // Deserialize payload -> Value                                  
            const auto* dataPtr = static_cast<const std::uint8_t*>(zmq_msg_data(&payloadMsg));                                  
            const auto  size    = static_cast<std::size_t>(zmq_msg_size(&payloadMsg));
            
            Value value;
            if (serializer_) {
                value = serializer_->deserialize(dataPtr, size);
            } else {
                Logger::warning(name() + ": no serializer set; cannot decode frame");
                zmq_msg_close(&payloadMsg);
                return false;
            }

            zmq_msg_close(&payloadMsg);

            // Value must be a dict to construct a Frame
            if (value.type() != Value::Type::Dict) {
                Logger::warning(name() + ": deserialized value is not a dict");
                return false;
            }

            auto framePtr = Frame::fromDict(value.asDict());
            if (!framePtr) {
                Logger::warning(name() + ": Frame::fromDict returned null");
                return false;
            }

            outFrame = std::move(framePtr);
            return true;
        }
    }
}

void ZmqSubscriber::transportClose() {
    Logger::debug("ZmqSubscriber is closing.");

    if (socket_) {
        if (zmq_close(socket_) != 0) {
            Logger::warning("ZmqSubscriber socket close error");
        }
        socket_ = nullptr;
    }

    if (context_ && ownsContext_) {
        if (zmq_ctx_term(context_) != 0) {
            Logger::warning("ZmqSubscriber context close error");
        }
        context_ = nullptr;
    }
}

} // namespace magpie
