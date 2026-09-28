#include <magpie/transport/webrtc_zmq_signaler.hpp>

#include <zmq.h>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace magpie {

ZmqSignaler::ZmqSignaler(std::string endpoint, std::string sessionId, bool bind)
    : endpoint_(std::move(endpoint)), sessionId_(std::move(sessionId)), bind_(bind) {
    if (endpoint_.empty() || sessionId_.empty())
        throw std::invalid_argument("ZmqSignaler: endpoint and session ID are required");
    std::promise<void> ready;
    auto future = ready.get_future();
    worker_ = std::thread([this, ready = std::move(ready)]() mutable { run(std::move(ready)); });
    try { future.get(); }
    catch (...) { stopped_ = true; worker_.join(); throw; }
}

ZmqSignaler::~ZmqSignaler() { disconnect(); }

void ZmqSignaler::publish(const std::uint8_t* data, std::size_t size) {
    if (stopped_) throw std::runtime_error("ZmqSignaler: disconnected");
    if (!data && size) throw std::invalid_argument("ZmqSignaler: null payload");
    std::lock_guard<std::mutex> lock(mutex_);
    outgoing_.emplace_back();
    if (size) outgoing_.back().assign(data, data + size);
}

void ZmqSignaler::subscribe(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

void ZmqSignaler::unsubscribe() {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = {};
}

void ZmqSignaler::disconnect() {
    stopped_ = true;
    if (worker_.joinable()) worker_.join();
    unsubscribe();
}

void ZmqSignaler::run(std::promise<void> ready) {
    void* context = zmq_ctx_new();
    if (!context) {
        ready.set_exception(std::make_exception_ptr(std::runtime_error("zmq_ctx_new failed")));
        return;
    }
    void* socket = zmq_socket(context, ZMQ_PAIR);
    if (!socket) {
        ready.set_exception(std::make_exception_ptr(std::runtime_error("zmq_socket failed")));
        zmq_ctx_term(context);
        return;
    }
    int linger = 0;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    const int rc = bind_ ? zmq_bind(socket, endpoint_.c_str())
                         : zmq_connect(socket, endpoint_.c_str());
    if (rc != 0) {
        ready.set_exception(std::make_exception_ptr(std::runtime_error("ZmqSignaler: bind/connect failed: " + std::string(zmq_strerror(zmq_errno())))));
        zmq_close(socket);
        zmq_ctx_term(context);
        return;
    }
    ready.set_value();

    while (!stopped_) {
        bool pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending = !outgoing_.empty();
        }
        zmq_pollitem_t item{socket, 0, static_cast<short>(ZMQ_POLLIN | (pending ? ZMQ_POLLOUT : 0)), 0};
        if (zmq_poll(&item, 1, 50) < 0) continue;
        if (item.revents & ZMQ_POLLIN) {
            zmq_msg_t msg;
            zmq_msg_init(&msg);
            if (zmq_msg_recv(&msg, socket, ZMQ_DONTWAIT) >= 0) {
                MessageCallback callback;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    callback = callback_;
                }
                if (callback) {
                    try { callback(static_cast<const std::uint8_t*>(zmq_msg_data(&msg)), zmq_msg_size(&msg)); }
                    catch (...) { /* user callback must not terminate the worker */ }
                }
            }
            zmq_msg_close(&msg);
        }
        if (item.revents & ZMQ_POLLOUT) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!outgoing_.empty()) {
                const auto& bytes = outgoing_.front();
                if (zmq_send(socket, bytes.data(), bytes.size(), ZMQ_DONTWAIT) >= 0)
                    outgoing_.pop_front();
            }
        }
    }
    zmq_close(socket);
    zmq_ctx_term(context);
}

} // namespace magpie
