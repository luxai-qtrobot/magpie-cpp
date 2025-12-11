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
 * StreamReader
 *
 * Reads (Frame, topic) from a transport.
 * If queueSize > 0, runs a background thread that fills a bounded queue.
 * If queueSize <= 0, read() calls the transport directly.
 */
class StreamReader {
public:
    using Item = std::pair<std::unique_ptr<Frame>, std::string>;

    explicit StreamReader(std::string name = "StreamReader",
                          int queueSize = 1);
    virtual ~StreamReader();

    StreamReader(const StreamReader&)            = delete;
    StreamReader& operator=(const StreamReader&) = delete;

    /**
     * Read one item.
     *
     * @param outFrame   Output unique_ptr<Frame>.
     * @param outTopic   Output topic string.
     * @param timeoutSec < 0: wait forever; >= 0: timeout in seconds.
     * @return true if an item was read, false on timeout or closed.
     */
    bool read(std::unique_ptr<Frame>& outFrame,
              std::string& outTopic,
              double timeoutSec = -1.0);

    /**
     * Close the reader and stop background thread.
     * Safe to call multiple times.
     */
    void close();

protected:
    // Blocking transport read; must be implemented by subclasses.
    virtual bool transportReadBlocking(std::unique_ptr<Frame>& outFrame,
                                       std::string& outTopic,
                                       double timeoutSec) = 0;

    // Transport-specific close.
    virtual void transportClose() = 0;

    const std::string& name() const noexcept { return name_; }

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
