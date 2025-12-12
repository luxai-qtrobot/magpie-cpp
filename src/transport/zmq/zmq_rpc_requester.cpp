#include <magpie/transport/zmq_rpc_requester.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

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
// Ctor / Dtor
// ---------------------------------------------------------------------

ZmqRpcRequester::ZmqRpcRequester(const std::string& endpoint,
                                 std::shared_ptr<Serializer> serializer,                                 
                                 const std::string& identity,
                                 double ackTimeoutSec)
    : RpcRequester("ZmqRpcRequester")
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

    // Build envelope: {"rid": rid, "payload": request}
    Value::Dict env;
    const std::string rid = getUniqueId();
    env["rid"]     = Value::fromString(rid);
    env["payload"] = request;
    Value envVal   = Value::fromDict(env);

    // Serialize and send
    auto bytes = serializer_->serialize(envVal);
    int rc = zmq_send(socket_, bytes.data(), static_cast<int>(bytes.size()), 0);
    if (rc == -1) {
        Logger::warning(name() + ": transport error during RPC call (send)");
        throw std::runtime_error(name() + ": zmq_send failed");
    }

    // ---- Wait for ACK: {"rid": rid, "ack": true} ----
    Object ackObj;
    try {
        double actualAckTimeout = ackTimeoutSec_;
        if (timeoutSec >= 0.0) {
            actualAckTimeout = std::min(timeoutSec, ackTimeoutSec_);
        }
        bool gotAck = socketReceive(ackObj, actualAckTimeout);
        if (!gotAck) {
            throw AckTimeoutError(name() + ": socket closed before ack");
        }
    } catch (const TimeoutError&) {
        throw AckTimeoutError(name() + ": no ack received within " +
                              std::to_string(ackTimeoutSec_) + " seconds");
    }

    if (ackObj.type() != Value::Type::Dict) {
        throw std::runtime_error(name() + ": invalid ack (not a dict)");
    }

    const auto& ackDict = ackObj.asDict();
    auto itRid = ackDict.find("rid");
    auto itAck = ackDict.find("ack");
    if (itRid == ackDict.end() ||
        itAck == ackDict.end() ||
        itRid->second.type() != Value::Type::String ||
        itAck->second.type() != Value::Type::Bool ||
        itRid->second.asString() != rid ||
        !itAck->second.asBool()) {
        throw std::runtime_error(name() + ": invalid ack received");
    }

    // ---- Wait for reply: {"rid": rid, "payload": ...} ----
    Object replyObj;
    try {
        bool gotReply = socketReceive(replyObj, timeoutSec);
        if (!gotReply) {
            throw ReplyTimeoutError(name() + ": socket closed before reply");
        }
    } catch (const TimeoutError&) {
        throw ReplyTimeoutError(name() + ": no reply received within " +
                                std::to_string(timeoutSec) + " seconds");
    }

    if (replyObj.type() != Value::Type::Dict) {
        throw std::runtime_error(name() + ": invalid reply (not a dict)");
    }

    const auto& repDict = replyObj.asDict();
    auto itRidRep  = repDict.find("rid");
    auto itPayload = repDict.find("payload");

    if (itRidRep == repDict.end() ||
        itPayload == repDict.end() ||
        itRidRep->second.type() != Value::Type::String ||
        itRidRep->second.asString() != rid) {
        throw std::runtime_error(name() + ": invalid reply received");
    }

    // Return just the payload (like Python)
    return itPayload->second;
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
    Logger::debug(name() + " is closing ZMQ DEALER socket.");

    if (socket_) {
        int linger = 0;
        zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(linger));
        if (zmq_close(socket_) != 0) {
            Logger::warning(name() + ": socket close error");
        }
        socket_ = nullptr;
    }

    if (context_ && ownsContext_) {
        if (zmq_ctx_term(context_) != 0) {
            Logger::warning(name() + ": context close error");
        }
        context_ = nullptr;
    }
}

} // namespace magpie
