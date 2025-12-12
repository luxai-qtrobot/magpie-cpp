#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <magpie/frames/frame.hpp>

namespace magpie {

/**
 * StreamWriter
 *
 * Writes (Frame, topic) to a transport.
 * If queueSize > 0, a background thread drains a bounded queue.
 * If queueSize <= 0, write() calls the transport directly.
 */
class StreamWriter {
public:
    using Item = std::pair<std::unique_ptr<Frame>, std::string>;

    explicit StreamWriter(std::string name = "StreamWriter",
                          int queueSize = 1);
    virtual ~StreamWriter();

    StreamWriter(const StreamWriter&)            = delete;
    StreamWriter& operator=(const StreamWriter&) = delete;

    /**
     * Write a frame with optional topic.
     * If queueSize > 0, this enqueues and returns quickly.
     * If queueSize <= 0, this calls transportWrite() synchronously.
     */
    void write(const Frame& frame, const std::string& topic = std::string());

    /**
     * Close the writer and flush remaining items.
     * Safe to call multiple times.
     */
    void close();

    const std::string& name() const noexcept { return name_; }

protected:
    // Transport-specific write.
    virtual void transportWrite(const Frame& frame,
                                const std::string& topic) = 0;

    // Transport-specific close.
    virtual void transportClose() = 0;
    

private:
    void threadLoop();

    std::string              name_;
    int                      queueSize_;
    std::atomic<bool>        closed_{false};
    std::atomic<bool>        closeRequested_{false};
    std::thread              thread_;

    std::mutex               mutex_;
    std::condition_variable  cv_;
    std::deque<Item>         queue_;
    std::size_t              maxQueueSize_{0};
};

} // namespace magpie
