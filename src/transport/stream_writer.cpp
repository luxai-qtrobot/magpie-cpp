#include <magpie/transport/stream_writer.hpp>
#include <magpie/frames/frame.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>

namespace magpie {

StreamWriter::StreamWriter(std::string name, int queueSize)
    : name_{std::move(name)}
    , queueSize_{queueSize}
    , maxQueueSize_{queueSize > 0 ? static_cast<std::size_t>(queueSize) : 0}
{
    if (queueSize_ > 0) {
        thread_ = std::thread(&StreamWriter::threadLoop, this);
    }
}

StreamWriter::~StreamWriter() {
    close();
}

void StreamWriter::write(const Frame& frame, const std::string& topic) {
    if (closed_.load()) {
        Logger::debug(name_ + " write() after close; dropping frame");
        return;
    }

    if (queueSize_ <= 0) {
        // Direct write.
        try {
            transportWrite(frame, topic);
        } catch (const std::exception& e) {
            Logger::warning(name_ + " write (direct): " + std::string(e.what()));
        } catch (...) {
            Logger::warning(name_ + " write (direct): unknown exception");
        }
        return;
    }

    // Queue mode: clone the frame so we can store it safely
    std::unique_ptr<Frame> copy;
    try {
        copy = frame.clone();
    } catch (const std::exception& e) {
        Logger::warning(name_ + " write: clone() threw exception: " + std::string(e.what()));
        return;
    } catch (...) {
        Logger::warning(name_ + " write: clone() threw unknown exception");
        return;
    }

    if (!copy) {
        Logger::warning(name_ + " write: clone() returned null");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_) {
            queue_.pop_front(); // drop oldest
        }

        queue_.emplace_back(std::move(copy), topic);
    }

    cv_.notify_one();
}

void StreamWriter::close() {
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

void StreamWriter::threadLoop() {
    using namespace std::chrono_literals;

    while (true) {
        Item item;
        {
            std::unique_lock<std::mutex> lock(mutex_);

            cv_.wait_for(lock, 500ms, [this] {
                return !queue_.empty() || closeRequested_.load();
            });

            if (queue_.empty()) {
                if (closeRequested_.load()) {
                    break;
                }
                continue;
            }

            item = std::move(queue_.front());
            queue_.pop_front();
        }

        if (!item.first) {
            continue;
        }

        try {
            transportWrite(*item.first, item.second);
        } catch (const std::exception& e) {
            Logger::warning(name_ + " threadLoop: " + std::string(e.what()));
        } catch (...) {
            Logger::warning(name_ + " threadLoop: unknown exception");
        }
    }
}

} // namespace magpie
