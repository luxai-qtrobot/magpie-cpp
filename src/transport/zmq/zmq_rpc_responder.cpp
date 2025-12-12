#include <magpie/transport/zmq_rpc_responder.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

#include <zmq.h>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

static bool isValidRequestDict(const Value& v) {
    if (v.type() != Value::Type::Dict) return false;
    const auto& d = v.asDict();
    auto itRid = d.find("rid");
    auto itPay = d.find("payload");
    return itRid != d.end() && itPay != d.end() && itRid->second.type() == Value::Type::String;
}

bool ZmqRpcResponder::startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

zmq_ctx_t* ZmqRpcResponder::sharedInprocContext() {
    static zmq_ctx_t* ctx = []() -> zmq_ctx_t* {
        void* raw = zmq_ctx_new();
        return static_cast<zmq_ctx_t*>(raw);
    }();
    return ctx;
}

ZmqRpcResponder::ZmqRpcResponder(const std::string& endpoint,
                                 std::shared_ptr<Serializer> serializer,                                 
                                 bool bind)
    : RpcResponder("ZmqRpcResponder")
    , endpoint_{endpoint}
    , bind_{bind}
{
    // Default serializer: Msgpack (like Python)
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

    socket_ = zmq_socket(context_, ZMQ_ROUTER);
    if (!socket_) {
        Logger::error(name() + ": failed to create ROUTER socket");
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error(name() + ": zmq_socket failed");
    }

    int rc = 0;
    if (bind_) {
        rc = zmq_bind(socket_, endpoint_.c_str());
    } else {
        rc = zmq_connect(socket_, endpoint_.c_str());
    }

    if (rc != 0) {
        Logger::error(name() + ": failed to " + std::string(bind_ ? "bind" : "connect") +
                      " at " + endpoint_);
        zmq_close(socket_);
        socket_ = nullptr;
        if (ownsContext_) zmq_ctx_term(context_);
        throw std::runtime_error(name() + ": zmq_bind/connect failed");
    }

    Logger::debug(name() + " " + (bind_ ? "bound" : "connected") +
                  " ROUTER at " + endpoint_ + ".");
}

ZmqRpcResponder::~ZmqRpcResponder() {
    close();
}

void ZmqRpcResponder::transportRecv(Object& outRequest,
                                    ClientContext& outClientCtx,
                                    double timeoutSec)
{
    if (!socket_) throw std::runtime_error(name() + ": socket is null");
    if (!serializer_) throw std::runtime_error(name() + ": no serializer set");

    Object reqObj;
    std::vector<std::uint8_t> identity;

    // Wait for a valid request
    bool got = socketReceive(reqObj, identity, timeoutSec);
    if (!got) {
        // socket closed
        throw std::runtime_error(name() + ": socket closed");
    }

    if (!isValidRequestDict(reqObj)) {
        throw std::runtime_error(name() + ": invalid request format");
    }

    const auto& d   = reqObj.asDict();
    const auto& rid = d.at("rid").asString();

    // Build client ctx (identity + rid)
    auto ctx = std::make_shared<ClientCtxData>();
    ctx->identity = std::move(identity);
    ctx->rid = rid;
    outClientCtx = std::static_pointer_cast<void>(ctx);

    // Send ACK: {"rid": rid, "ack": true}
    try {
        Value::Dict ack;
        ack["rid"] = Value::fromString(rid);
        ack["ack"] = Value::fromBool(true);
        Value ackVal = Value::fromDict(ack);

        auto bytes = serializer_->serialize(ackVal);

        zmq_msg_t idMsg;
        zmq_msg_t payloadMsg;
        zmq_msg_init_size(&idMsg, ctx->identity.size());
        std::memcpy(zmq_msg_data(&idMsg), ctx->identity.data(), ctx->identity.size());

        zmq_msg_init_size(&payloadMsg, bytes.size());
        std::memcpy(zmq_msg_data(&payloadMsg), bytes.data(), bytes.size());

        // identity frame (more), then payload frame
        int rc1 = zmq_msg_send(&idMsg, socket_, ZMQ_SNDMORE);
        int rc2 = zmq_msg_send(&payloadMsg, socket_, 0);

        zmq_msg_close(&idMsg);
        zmq_msg_close(&payloadMsg);

        if (rc1 == -1 || rc2 == -1) {
            throw std::runtime_error("zmq_msg_send failed");
        }
    } catch (const std::exception& e) {
        Logger::warning(name() + ": transport error during ack send: " + std::string(e.what()));
        throw;
    }

    // Return request payload only (like Python)
    outRequest = d.at("payload");
}

void ZmqRpcResponder::transportSend(const Object& response,
                                   const ClientContext& clientCtx)
{
    if (!socket_) throw std::runtime_error(name() + ": socket is null");
    if (!serializer_) throw std::runtime_error(name() + ": no serializer set");
    if (!clientCtx) throw std::runtime_error(name() + ": clientCtx is null");

    auto ctx = std::static_pointer_cast<ClientCtxData>(clientCtx);

    // Reply: {"rid": ctx->rid, "payload": response}
    Value::Dict rep;
    rep["rid"]     = Value::fromString(ctx->rid);
    rep["payload"] = response;
    Value repVal   = Value::fromDict(rep);

    auto bytes = serializer_->serialize(repVal);

    zmq_msg_t idMsg;
    zmq_msg_t payloadMsg;

    zmq_msg_init_size(&idMsg, ctx->identity.size());
    std::memcpy(zmq_msg_data(&idMsg), ctx->identity.data(), ctx->identity.size());

    zmq_msg_init_size(&payloadMsg, bytes.size());
    std::memcpy(zmq_msg_data(&payloadMsg), bytes.data(), bytes.size());

    int rc1 = zmq_msg_send(&idMsg, socket_, ZMQ_SNDMORE);
    int rc2 = zmq_msg_send(&payloadMsg, socket_, 0);

    zmq_msg_close(&idMsg);
    zmq_msg_close(&payloadMsg);

    if (rc1 == -1 || rc2 == -1) {
        Logger::warning(name() + ": transport error during send");
        throw std::runtime_error(name() + ": zmq_msg_send failed");
    }
}

bool ZmqRpcResponder::socketReceive(Object& outObj,
                                   std::vector<std::uint8_t>& outIdentity,
                                   double timeoutSec)
{
    if (!socket_ || !context_) {
        Logger::debug(name() + ": socket/context null, stop reading.");
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
        long pollMs = 1000;

        if (timeoutSec >= 0.0) {
            double elapsed =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    clock::now() - start).count();
            double remain = timeoutSec - elapsed;
            if (remain <= 0.0) {
                throw TimeoutError(name() + ": no request received within " +
                                   std::to_string(timeoutSec) + " seconds");
            }
            pollMs = static_cast<long>(remain * 1000.0);
            if (pollMs > 1000) pollMs = 1000;
            if (pollMs < 1)    pollMs = 1;
        }

        int rc = zmq_poll(items, 1, pollMs);
        if (rc == -1) {
            if (!socket_) return false;
            Logger::warning(name() + ": transport error during recv (zmq_poll)");
            throw std::runtime_error(name() + ": zmq_poll failed");
        }

        if (items[0].revents & ZMQ_POLLIN) {
            // ROUTER: [identity][payload]
            zmq_msg_t idMsg;
            zmq_msg_t payloadMsg;
            zmq_msg_init(&idMsg);
            zmq_msg_init(&payloadMsg);

            int r1 = zmq_msg_recv(&idMsg, socket_, 0);
            if (r1 == -1) {
                zmq_msg_close(&idMsg);
                zmq_msg_close(&payloadMsg);
                if (!socket_) return false;
                continue;
            }

            int more = 0;
            size_t moreSize = sizeof(more);
            zmq_getsockopt(socket_, ZMQ_RCVMORE, &more, &moreSize);
            if (!more) {
                zmq_msg_close(&idMsg);
                zmq_msg_close(&payloadMsg);
                throw std::runtime_error(name() + ": invalid message format, expected [identity, payload]");
            }

            int r2 = zmq_msg_recv(&payloadMsg, socket_, 0);
            if (r2 == -1) {
                zmq_msg_close(&idMsg);
                zmq_msg_close(&payloadMsg);
                if (!socket_) return false;
                continue;
            }

            outIdentity.assign(
                static_cast<const std::uint8_t*>(zmq_msg_data(&idMsg)),
                static_cast<const std::uint8_t*>(zmq_msg_data(&idMsg)) + zmq_msg_size(&idMsg)
            );

            const auto* dataPtr = static_cast<const std::uint8_t*>(zmq_msg_data(&payloadMsg));
            const auto  size    = static_cast<std::size_t>(zmq_msg_size(&payloadMsg));

            zmq_msg_close(&idMsg);
            zmq_msg_close(&payloadMsg);

            outObj = serializer_->deserialize(dataPtr, size);
            return true;
        }

        if (timeoutSec < 0.0) {
            continue;
        }
    }
}

void ZmqRpcResponder::transportClose() {
    Logger::debug(name() + " is closing ZMQ ROUTER socket.");

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
