#include <magpie/transport/zmq_rpc_requester.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zmq.h>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/common.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

bool ZmqRpcRequester::startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

zmq_ctx_t* ZmqRpcRequester::sharedInprocContext() {
    static zmq_ctx_t* ctx = []() -> zmq_ctx_t* {
        void* raw = zmq_ctx_new();
        return static_cast<zmq_ctx_t*>(raw);
    }();
    return ctx;
}

// ---------------------------------------------------------------------
// Internal: Demux state
// ---------------------------------------------------------------------

struct ZmqRpcRequester::PendingCall {
    std::mutex              mtx;
    std::condition_variable cvAck;
    std::condition_variable cvReply;

    bool acked   = false;
    bool replied = false;

    Object      replyPayload;
    std::string errorMsg;  // set on invalid messages/transport close
};

// ---------------------------------------------------------------------
// Ctor / Dtor
// ---------------------------------------------------------------------

ZmqRpcRequester::ZmqRpcRequester(const std::string& endpoint,
                                 std::shared_ptr<Serializer> serializer,
                                 const std::string& identity,
                                 double ackTimeoutSec,
                                 std::shared_ptr<BaseSchema> schema)
    : RpcRequester("ZmqRpcRequester", std::move(schema))
    , endpoint_{endpoint}
    , ackTimeoutSec_{ackTimeoutSec}
{
    // Default to MsgpackSerializer, like Python
    if (!serializer) {
        serializer = std::make_shared<MsgpackSerializer>();
    }
    serializer_ = std::move(serializer);

    const bool inproc = startsWith(endpoint_, "inproc:");

    if (inproc) {
        context_     = sharedInprocContext();
        ownsContext_ = false;
    } else {
        context_     = static_cast<zmq_ctx_t*>(zmq_ctx_new());
        ownsContext_ = true;
    }

    if (!context_) {
        Logger::error(name() + ": failed to create ZeroMQ context");
        throw std::runtime_error(name() + ": zmq_ctx_new failed");
    }

    socket_ = zmq_socket(context_, ZMQ_DEALER);
    if (!socket_) {
        Logger::error(name() + ": failed to create DEALER socket");
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error(name() + ": zmq_socket failed");
    }

    if (!identity.empty()) {
        zmq_setsockopt(socket_, ZMQ_IDENTITY, identity.data(), identity.size());
    }

    int rc = zmq_connect(socket_, endpoint_.c_str());
    if (rc != 0) {
        Logger::error(name() + ": connect failed for endpoint " + endpoint_);
        zmq_close(socket_);
        socket_ = nullptr;
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error(name() + ": zmq_connect failed");
    }

    // ----------------------------
    // Demux + single I/O thread
    // ----------------------------
    closing_ = false;

    // Control channel (callers -> I/O thread) so we can poll both sockets.
    // NOTE: callers never touch DEALER; only I/O thread owns it.
    ctrlEndpoint_ = "inproc://zmq-rpc-req-" + getUniqueId();

    ctrlPull_ = zmq_socket(context_, ZMQ_PULL);
    if (!ctrlPull_) {
        Logger::error(name() + ": failed to create control PULL socket");
        zmq_close(socket_);
        socket_ = nullptr;
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error(name() + ": zmq_socket(ctrlPull) failed");
    }
    rc = zmq_bind(ctrlPull_, ctrlEndpoint_.c_str());
    if (rc != 0) {
        Logger::error(name() + ": failed to bind control socket at " + ctrlEndpoint_);
        zmq_close(ctrlPull_);
        ctrlPull_ = nullptr;
        zmq_close(socket_);
        socket_ = nullptr;
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error(name() + ": zmq_bind(ctrlPull) failed");
    }

    ioThread_ = std::thread([this]() { this->ioLoop_(); });

    Logger::debug(name() + " connected to " + endpoint_ + " as DEALER.");
}

ZmqRpcRequester::~ZmqRpcRequester() {
    close();
}

// ---------------------------------------------------------------------
// transportCall
// ---------------------------------------------------------------------

ZmqRpcRequester::Object
ZmqRpcRequester::transportCall(const Object& request, double timeoutSec) {
    if (!socket_) {
        throw std::runtime_error(name() + ": socket is null");
    }
    if (!serializer_) {
        throw std::runtime_error(name() + ": no serializer set");
    }
    if (closing_) {
        throw std::runtime_error(name() + ": transport is closed");
    }

    // Build envelope: {"rid": rid, "payload": request}
    Value::Dict env;
    const std::string rid = getUniqueId();
    env["rid"]     = Value::fromString(rid);
    env["payload"] = request;
    Value envVal   = Value::fromDict(env);

    // Serialize (send is done by the I/O thread)
    auto bytes = serializer_->serialize(envVal);

    // Register pending BEFORE send (so we can catch very fast ACK/reply).
    auto pending = std::make_shared<PendingCall>();
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_[rid] = pending;
    }

    // Send to I/O thread (do not touch DEALER from caller threads)
    try {
        sendToIo_(bytes.data(), bytes.size());
    } catch (const std::exception& e) {
        Logger::warning(name() + ": transport error during RPC call (sendToIo)");
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.erase(rid);
        throw;
    }

    // ---- Wait for ACK: {"rid": rid, "ack": true} ----
    double actualAckTimeout = ackTimeoutSec_;
    if (timeoutSec >= 0.0) {
        actualAckTimeout = std::min(timeoutSec, ackTimeoutSec_);
    }

    {
        std::unique_lock<std::mutex> lk(pending->mtx);
        if (actualAckTimeout >= 0.0) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                      std::chrono::duration<double>(actualAckTimeout));
            while (!pending->acked && pending->errorMsg.empty()) {
                if (pending->cvAck.wait_until(lk, deadline) == std::cv_status::timeout) {
                    break;
                }
            }
            if (!pending->acked && pending->errorMsg.empty()) {
                // timeout
                std::lock_guard<std::mutex> lk2(pendingMtx_);
                pending_.erase(rid);
                throw AckTimeoutError(name() + ": no ack received within " +
                                      std::to_string(actualAckTimeout) + " seconds");
            }
        } else {
            // wait forever
            while (!pending->acked && pending->errorMsg.empty()) {
                pending->cvAck.wait(lk);
            }
        }

        if (!pending->errorMsg.empty() && !pending->acked) {
            std::lock_guard<std::mutex> lk2(pendingMtx_);
            pending_.erase(rid);
            throw std::runtime_error(pending->errorMsg);
        }
    }

    // ---- Wait for reply: {"rid": rid, "payload": ...} ----
    {
        std::unique_lock<std::mutex> lk(pending->mtx);
        if (timeoutSec >= 0.0) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                      std::chrono::duration<double>(timeoutSec));
            while (!pending->replied && pending->errorMsg.empty()) {
                if (pending->cvReply.wait_until(lk, deadline) == std::cv_status::timeout) {
                    break;
                }
            }
            if (!pending->replied && pending->errorMsg.empty()) {
                std::lock_guard<std::mutex> lk2(pendingMtx_);
                pending_.erase(rid);
                throw ReplyTimeoutError(name() + ": no reply received within " +
                                        std::to_string(timeoutSec) + " seconds");
            }
        } else {
            // wait forever
            while (!pending->replied && pending->errorMsg.empty()) {
                pending->cvReply.wait(lk);
            }
        }

        if (!pending->errorMsg.empty() && !pending->replied) {
            std::lock_guard<std::mutex> lk2(pendingMtx_);
            pending_.erase(rid);
            throw std::runtime_error(pending->errorMsg);
        }
    }

    // Remove from map and return just the payload (like Python)
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.erase(rid);
    }
    return pending->replyPayload;
}

// ---------------------------------------------------------------------
// socketReceive
// ---------------------------------------------------------------------

bool ZmqRpcRequester::socketReceive(Object& outObj, double timeoutSec) {
    if (!socket_ || !context_) {
        Logger::debug(name() + ": socket/context null, stop reading.");
        return false;
    }
    if (!serializer_) {
        throw std::runtime_error(name() + ": no serializer set");
    }

    using clock = std::chrono::steady_clock;
    auto start  = clock::now();

    zmq_pollitem_t items[1];
    items[0].socket  = socket_;
    items[0].fd      = 0;
    items[0].events  = ZMQ_POLLIN;
    items[0].revents = 0;

    while (true) {
        long pollMs = 1000;

        if (timeoutSec >= 0.0) {
            double elapsed =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    clock::now() - start).count();
            double remain = timeoutSec - elapsed;
            if (remain <= 0.0) {
                throw TimeoutError(name() + ": no response received within " +
                                   std::to_string(timeoutSec) + " seconds");
            }
            pollMs = static_cast<long>(remain * 1000.0);
            if (pollMs > 1000) pollMs = 1000;
            if (pollMs < 1)    pollMs = 1;
        }

        int rc = zmq_poll(items, 1, pollMs);
        if (rc == -1) {
            if (!socket_) {
                return false;
            }
            Logger::warning(name() + ": transport error during recv (zmq_poll)");
            throw std::runtime_error(name() + ": zmq_poll failed");
        }

        if (items[0].revents & ZMQ_POLLIN) {
            zmq_msg_t msg;
            zmq_msg_init(&msg);
            rc = zmq_msg_recv(&msg, socket_, 0);
            if (rc == -1) {
                zmq_msg_close(&msg);
                if (!socket_) {
                    return false;
                }
                Logger::warning(name() + ": failed to receive RPC frame");
                continue;
            }

            const auto* dataPtr =
                static_cast<const std::uint8_t*>(zmq_msg_data(&msg));
            const auto  size =
                static_cast<std::size_t>(zmq_msg_size(&msg));

            try {
                outObj = serializer_->deserialize(dataPtr, size);
            } catch (...) {
                zmq_msg_close(&msg);
                throw;
            }

            zmq_msg_close(&msg);
            return true;
        }

        if (timeoutSec < 0.0) {
            continue;  // wait forever, chunked by pollMs
        }
    }
}

// ---------------------------------------------------------------------
// transportClose
// ---------------------------------------------------------------------

void ZmqRpcRequester::transportClose() {
    Logger::debug(name() + " is closing sockets.");

    closing_ = true;

    // Wake the I/O loop so it can exit (no polling timeouts needed)
    try {
        sendCloseToIo_();
    } catch (...) {
        // best effort
    }

    // Join I/O thread
    try {
        if (ioThread_.joinable()) {
            ioThread_.join();
        }
    } catch (...) {
        Logger::warning(name() + ": I/O thread join error");
    }

    // Close socket immediately without waiting for peer
    if (socket_) {
        int linger = 0;
        zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(linger));
        if (zmq_close(socket_) != 0) {
            Logger::warning(name() + ": socket close error");
        }
        socket_ = nullptr;
    }

    if (ctrlPull_) {
        int linger = 0;
        zmq_setsockopt(ctrlPull_, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_close(ctrlPull_);
        ctrlPull_ = nullptr;
    }

    // Fail/unblock any pending calls so user code doesn't hang forever
    failAllPending_(name() + ": transport closed");

    if (context_ && ownsContext_) {
        if (zmq_ctx_term(context_) != 0) {
            Logger::warning(name() + ": context close error");
        }
        context_ = nullptr;
    }
}

// ---------------------------------------------------------------------
// Internal: control channel + I/O loop
// ---------------------------------------------------------------------

void ZmqRpcRequester::sendToIo_(const std::uint8_t* data, std::size_t size) {
    if (!context_) {
        throw std::runtime_error(name() + ": context is null");
    }

    // Create a short-lived PUSH socket (avoid thread_local socket lifetime issues on shutdown).
    void* s = zmq_socket(context_, ZMQ_PUSH);
    if (!s) {
        throw std::runtime_error(name() + ": zmq_socket(ctrlPush) failed");
    }

    int rc = zmq_connect(s, ctrlEndpoint_.c_str());
    if (rc != 0) {
        int linger = 0;
        zmq_setsockopt(s, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_close(s);
        throw std::runtime_error(name() + ": zmq_connect(ctrlPush) failed");
    }

    // One frame: raw serialized request bytes
    rc = zmq_send(s, data, static_cast<int>(size), 0);

    int linger = 0;
    zmq_setsockopt(s, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_close(s);

    if (rc == -1) {
        throw std::runtime_error(name() + ": zmq_send(ctrlPush) failed");
    }
}

void ZmqRpcRequester::sendCloseToIo_() {
    if (!context_) return;

    void* s = zmq_socket(context_, ZMQ_PUSH);
    if (!s) return;
    if (zmq_connect(s, ctrlEndpoint_.c_str()) == 0) {
        const char* kClose = "__CLOSE__";
        zmq_send(s, kClose, static_cast<int>(std::strlen(kClose)), 0);
    }
    int linger = 0;
    zmq_setsockopt(s, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_close(s);
}

void ZmqRpcRequester::failAllPending_(const std::string& err) {
    std::unordered_map<std::string, std::shared_ptr<PendingCall>> local;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        local.swap(pending_);
    }
    for (auto& kv : local) {
        auto& p = kv.second;
        {
            std::lock_guard<std::mutex> lk(p->mtx);
            if (p->errorMsg.empty()) p->errorMsg = err;
        }
        p->cvAck.notify_all();
        p->cvReply.notify_all();
    }
}

void ZmqRpcRequester::ioLoop_() {
    Logger::debug(name() + ": I/O loop started.");

    zmq_pollitem_t items[2];
    items[0].socket  = socket_;
    items[0].fd      = 0;
    items[0].events  = ZMQ_POLLIN;
    items[0].revents = 0;

    items[1].socket  = ctrlPull_;
    items[1].fd      = 0;
    items[1].events  = ZMQ_POLLIN;
    items[1].revents = 0;

    const std::string closeSentinel = "__CLOSE__";

    while (true) {
        int rc = zmq_poll(items, 2, -1);  // block until dealer or control has data
        if (rc == -1) {
            if (closing_) break;
            Logger::warning(name() + ": transport error during poll in I/O loop");
            break;
        }

        // ---- Outgoing control messages ----
        if (items[1].revents & ZMQ_POLLIN) {
            zmq_msg_t msg;
            zmq_msg_init(&msg);
            rc = zmq_msg_recv(&msg, ctrlPull_, 0);
            if (rc == -1) {
                zmq_msg_close(&msg);
                if (closing_) break;
                Logger::warning(name() + ": control recv error");
            } else {
                const auto* dataPtr =
                    static_cast<const std::uint8_t*>(zmq_msg_data(&msg));
                const auto  size =
                    static_cast<std::size_t>(zmq_msg_size(&msg));

                if (size == closeSentinel.size() &&
                    std::memcmp(dataPtr, closeSentinel.data(), size) == 0) {
                    zmq_msg_close(&msg);
                    break;
                }

                // Forward raw request bytes to DEALER
                int sRc = zmq_send(socket_, dataPtr, static_cast<int>(size), 0);
                zmq_msg_close(&msg);

                if (sRc == -1) {
                    Logger::warning(name() + ": transport error during send in I/O loop");
                    failAllPending_(name() + ": transport send failure");
                    break;
                }
            }
        }

        // ---- Incoming replies (ACK or final reply) ----
        if (items[0].revents & ZMQ_POLLIN) {
            zmq_msg_t msg;
            zmq_msg_init(&msg);
            rc = zmq_msg_recv(&msg, socket_, 0);
            if (rc == -1) {
                zmq_msg_close(&msg);
                if (closing_) break;
                Logger::warning(name() + ": failed to receive RPC frame");
                continue;
            }

            const auto* dataPtr =
                static_cast<const std::uint8_t*>(zmq_msg_data(&msg));
            const auto  size =
                static_cast<std::size_t>(zmq_msg_size(&msg));

            Object obj;
            try {
                obj = serializer_->deserialize(dataPtr, size);
            } catch (...) {
                zmq_msg_close(&msg);
                continue;
            }
            zmq_msg_close(&msg);

            if (obj.type() != Value::Type::Dict) {
                continue;
            }

            const auto& d = obj.asDict();
            auto itRid = d.find("rid");
            if (itRid == d.end() || itRid->second.type() != Value::Type::String) {
                continue;
            }
            const std::string rid = itRid->second.asString();

            std::shared_ptr<PendingCall> pending;
            {
                std::lock_guard<std::mutex> lk(pendingMtx_);
                auto it = pending_.find(rid);
                if (it != pending_.end()) pending = it->second;
            }
            if (!pending) {
                // late/unknown message
                continue;
            }

            // ACK message
            auto itAck = d.find("ack");
            if (itAck != d.end()) {
                bool ok = (itAck->second.type() == Value::Type::Bool) && itAck->second.asBool();
                {
                    std::lock_guard<std::mutex> lk(pending->mtx);
                    if (!ok) {
                        pending->errorMsg = name() + ": invalid ack received";
                    } else {
                        pending->acked = true;
                    }
                }
                pending->cvAck.notify_all();
                continue;
            }

            // Reply message
            auto itPayload = d.find("payload");
            if (itPayload != d.end()) {
                {
                    std::lock_guard<std::mutex> lk(pending->mtx);
                    pending->replyPayload = itPayload->second;
                    pending->replied      = true;
                }
                pending->cvReply.notify_all();
                continue;
            }

            // Anything else: treat as invalid reply
            {
                std::lock_guard<std::mutex> lk(pending->mtx);
                pending->errorMsg = name() + ": invalid reply received";
            }
            pending->cvReply.notify_all();
        }
    }

    // Logger::debug(name() + ": I/O loop exiting.");
    failAllPending_(name() + ": transport closed");
}

} // namespace magpie
