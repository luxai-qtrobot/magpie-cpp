#pragma once

#include <memory>
#include <string>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/serializer.hpp>
#include <magpie/transport/stream_writer.hpp>

struct zmq_ctx_t;
struct zmq_socket_t;  // forward for C API (actually void*, but this is clearer)

namespace magpie {

/**
 * ZmqStreamWriter
 *
 * C++ equivalent of Python ZmqStreamWriter.
 * Publishes Frame objects to a ZeroMQ PUB socket using a Serializer.
 */
class ZmqStreamWriter : public StreamWriter {
public:
    ZmqStreamWriter(const std::string& endpoint,                 
                 int queueSize = 0,           // default: direct write, no worker thread (for now)
                 bool bind = true,
                 const std::string& delivery = "reliable",
                std::shared_ptr<Serializer> serializer = nullptr);
    ~ZmqStreamWriter() override;

    ZmqStreamWriter(const ZmqStreamWriter&)            = delete;
    ZmqStreamWriter& operator=(const ZmqStreamWriter&) = delete;

protected:
    void transportWrite(const Frame& frame,
                        const std::string& topic) override;

    void transportClose() override;

private:
    static zmq_ctx_t* sharedInprocContext();
    static bool startsWith(const std::string& s, const std::string& prefix);

    std::string                endpoint_;
    std::string                delivery_;
    std::shared_ptr<Serializer> serializer_;

    zmq_ctx_t*   context_{nullptr};
    void*        socket_{nullptr};  // underlying is void* from zmq
    bool         ownsContext_{false};
};

} // namespace magpie
