#pragma once

#include <memory>
#include <string>
#include <vector>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/serializer.hpp>
#include <magpie/transport/stream_reader.hpp>
#include <magpie/transport/timeout_error.hpp>

struct zmq_ctx_t;

namespace magpie {

/**
 * ZmqStreamReader
 *
 * C++ equivalent of Python ZmqStreamReader.
 * Subscribes to one or more topics and yields Frame objects.
 */
class ZmqStreamReader : public StreamReader {
public:
    ZmqStreamReader(const std::string& endpoint,
                  const std::vector<std::string>& topics,                  
                  int queueSize = 10,
                  bool bind = false,
                  const std::string& delivery = "reliable",
                  std::shared_ptr<Serializer> serializer = nullptr);

    // Convenience ctor: single topic string (empty = all topics)
    ZmqStreamReader(const std::string& endpoint,
                  const std::string& topic,                  
                  int queueSize = 10,
                  bool bind = false,
                  const std::string& delivery = "reliable",
                  std::shared_ptr<Serializer> serializer=nullptr);

    ~ZmqStreamReader() override;

    ZmqStreamReader(const ZmqStreamReader&)            = delete;
    ZmqStreamReader& operator=(const ZmqStreamReader&) = delete;

protected:
    bool transportReadBlocking(std::unique_ptr<Frame>& outFrame,
                               std::string& outTopic,
                               double timeoutSec) override;

    void transportClose() override;

private:
    static bool startsWith(const std::string& s, const std::string& prefix);

    std::string                 endpoint_;
    std::vector<std::string>    topics_;
    std::string                 delivery_;
    std::shared_ptr<Serializer> serializer_;

    zmq_ctx_t*   context_{nullptr};
    void*        socket_{nullptr};
    bool         ownsContext_{false};
};

} // namespace magpie
