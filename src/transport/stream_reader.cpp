
#include <magpie/transport/stream_reader.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/transport/timeout_error.hpp>

#include <chrono>

namespace magpie {

StreamReader::StreamReader(std::string name, int queueSize)
    : name_{std::move(name)}
    , queueSize_{queueSize}
    , maxQueueSize_{queueSize > 0 ? static_cast<std::size_t>(queueSize) : 0}
{
    if (queueSize_ > 0) {
        thread_ = std::thread(&StreamReader::threadLoop, this);
    }
}

StreamReader::~StreamReader() {
    close();
}

bool StreamReader::read(std::unique_ptr<Frame>& outFrame,
                        std::string& outTopic,
                        double timeoutSec) {
    if (queueSize_ <= 0) {
        // Directly read from transport.
        return transportReadBlocking(outFrame, outTopic, timeoutSec);
    }

    if (closed_.load()) {
        return false;
    }

    using clock = std::chrono::steady_clock;
    auto deadline = (timeoutSec >= 0.0)
                  ? clock::now() + std::chrono::duration<double>(timeoutSec)
                  : clock::time_point::max();

    std::unique_lock<std::mutex> lock(mutex_);

    while (true) {
        if (!queue_.empty()) {
            auto item = std::move(queue_.front());
            queue_.pop_front();
            outFrame = std::move(item.first);
            outTopic = std::move(item.second);
            return true;
        }

        if (closed_.load() || closeRequested_.load()) {
            return false;
        }

        if (timeoutSec >= 0.0) {
            if (!cv_.wait_until(lock, deadline, [this] {
                    return !queue_.empty() || closed_.load() || closeRequested_.load();
                })) {
                // timeout
                return false;
            }
        } else {
            cv_.wait(lock, [this] {
                return !queue_.empty() || closed_.load() || closeRequested_.load();
            });
        }
    }
}

void StreamReader::close() {
    bool already = closed_.exchange(true);
    if (already) return;

    closeRequested_.store(true);
    cv_.notify_all();

    if (queueSize_ > 0 && thread_.joinable()) {
        try {
            thread_.join();
        } catch (...) {
            // ignore
        }
    }

    try {
        transportClose();
    } catch (const std::exception& e) {
        Logger::warning(name_ + " close: " + std::string(e.what()));
    } catch (...) {
        Logger::warning(name_ + " close: unknown error");
    }
}

void StreamReader::threadLoop() {
    const double pollTimeoutSec = 1.0;

    while (!closeRequested_.load()) {
        std::unique_ptr<Frame> frame;
        std::string topic;

        bool ok = false;
        try {
            ok = transportReadBlocking(frame, topic, pollTimeoutSec);             //TimeoutError
        } catch (const TimeoutError& e) {
            continue;
        } catch (const std::exception& e) {
            Logger::warning(name_ + " threadLoop: " + std::string(e.what()));
            continue;
        } catch (...) {
            Logger::warning(name_ + " threadLoop: unknown exception");
            continue;
        }

        if (!ok || !frame) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_) {
                queue_.pop_front(); // drop oldest
            }

            queue_.emplace_back(std::move(frame), std::move(topic));
        }

        cv_.notify_one();
    }
}

} // namespace magpie
