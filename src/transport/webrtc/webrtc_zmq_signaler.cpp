#include <magpie/transport/webrtc_zmq_signaler.hpp>

#include <zmq.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace magpie {

ZmqSignaler::ZmqSignaler(std::string endpoint, std::string sessionId,
                         bool bind, bool multiplex)
    : endpoint_(std::move(endpoint)), sessionId_(std::move(sessionId)),
      bind_(bind), multiplex_(multiplex) {
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
    void* socket = zmq_socket(context,
        multiplex_ ? (bind_ ? ZMQ_ROUTER : ZMQ_DEALER) : ZMQ_PAIR);
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

    std::vector<std::vector<std::uint8_t>> identities;

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
                std::vector<std::uint8_t> identity;
                if (multiplex_ && bind_) {
                    if (!zmq_msg_more(&msg)) {
                        zmq_msg_close(&msg);
                        continue;
                    }
                    const auto* first = static_cast<const std::uint8_t*>(zmq_msg_data(&msg));
                    identity.assign(first, first + zmq_msg_size(&msg));
                    if (std::find(identities.begin(), identities.end(), identity) == identities.end())
                        identities.push_back(identity);
                    zmq_msg_close(&msg);
                    zmq_msg_init(&msg);
                    if (zmq_msg_recv(&msg, socket, ZMQ_DONTWAIT) < 0) {
                        zmq_msg_close(&msg);
                        continue;
                    }
                    const auto* data = static_cast<const std::uint8_t*>(zmq_msg_data(&msg));
                    const auto size = zmq_msg_size(&msg);
                    for (const auto& other : identities) {
                        if (other == identity) continue;
                        if (zmq_send(socket, other.data(), other.size(), ZMQ_SNDMORE | ZMQ_DONTWAIT) >= 0)
                            zmq_send(socket, data, size, ZMQ_DONTWAIT);
                    }
                }
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
                if (multiplex_ && bind_) {
                    for (const auto& identity : identities) {
                        if (zmq_send(socket, identity.data(), identity.size(), ZMQ_SNDMORE | ZMQ_DONTWAIT) >= 0)
                            zmq_send(socket, bytes.data(), bytes.size(), ZMQ_DONTWAIT);
                    }
                    outgoing_.pop_front();
                } else if (zmq_send(socket, bytes.data(), bytes.size(), ZMQ_DONTWAIT) >= 0) {
                    outgoing_.pop_front();
                }
            }
        }
    }
    zmq_close(socket);
    zmq_ctx_term(context);
}

} // namespace magpie
