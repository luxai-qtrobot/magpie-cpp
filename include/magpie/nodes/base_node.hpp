#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include <magpie/utils/logger.hpp>

namespace magpie {

/**
 * @brief BaseNode
 *
 * A minimal, safe, and professional C++ node base class.
 *
 * Key design goals:
 *  - BaseNode itself does NOT need to know the type of user configuration.
 *  - Still allow derived nodes to use strongly-typed configuration.
 *  - Avoid calling virtual methods from constructors (C++ rule).
 *  - Provide a predictable lifecycle: setup -> loop(process) -> cleanup.
 *
 * Lifecycle (safe in C++):
 *   1) Construct BaseNode (stores config/state, does NOT call virtuals)
 *   2) Derived constructor finishes
 *   3) User/derived calls start()
 *        - calls setup() once (virtual)
 *        - spawns worker thread
 *   4) Worker loop calls process() until terminate() requested
 *   5) cleanup() called after loop exits
 *
 * Configuration model (Option A: type-erasure):
 *  - BaseNode stores configuration as a type-erased shared_ptr<void>
 *  - BaseNode also stores a type_index for runtime type checking
 *  - Derived nodes can retrieve typed config with configAs<T>()
 *
 * This avoids templating BaseNode while keeping typed configs ergonomic.
 */
class BaseNode {
public:
    /**
     * @brief Construct a BaseNode with a typed config.
     *
     * The config is copied into an internal shared object (lifetime managed).
     *
     * @tparam Config  Any copyable config type.
     * @param config   The user configuration object.
     * @param paused   Whether the node starts paused.
     * @param name     Node name (used for logging/debugging).
     */
    template <typename Config>
    explicit BaseNode(const Config& config, bool paused = false, std::string name = "Node")
        : name_(std::move(name))
        , config_(std::make_shared<Config>(config))
        , configType_(typeid(Config))
    {
        paused_.store(paused);
    }

    virtual ~BaseNode() {
        // Best-effort deterministic cleanup
        terminate(1.0);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    BaseNode(const BaseNode&)            = delete;
    BaseNode& operator=(const BaseNode&) = delete;

    const std::string& name() const { return name_; }

    bool started() const noexcept { return started_.load(); }
    bool paused() const noexcept { return paused_.load(); }
    bool terminating() const noexcept { return terminating_.load(); }

    /**
     * @brief start()
     *
     * Explicitly starts the node:
     *  - calls setup() once
     *  - starts the worker thread
     *
     * Must NOT be called from BaseNode constructor (virtual dispatch rule).
     * Safe to call multiple times (only first call takes effect).
     */
    void start() {
        bool expected = false;
        if (!started_.compare_exchange_strong(expected, true)) {
            return; // already started
        }

        setup();
        worker_ = std::thread(&BaseNode::runLoop_, this);
    }

    void pause() {
        paused_.store(true);
        Logger::debug(name_ + " paused.");
    }

    void resume() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            paused_.store(false);
        }
        cv_.notify_all();
        Logger::debug(name_ + " resumed.");
    }

    /**
     * @brief terminate(timeoutSec)
     *
     * Idempotent. Safe to call multiple times.
     *
     * Behavior:
     *  - sets terminating flag
     *  - resumes if paused (so loop can exit)
     *  - calls interrupt() hook to unblock I/O in derived classes
     *  - optionally waits for run loop to exit
     *
     * @param timeoutSec Negative => wait indefinitely.
     */
    void terminate(double timeoutSec = -1.0) {
        bool expected = false;
        if (!terminated_.compare_exchange_strong(expected, true)) {
            return; // already terminated
        }

        terminating_.store(true);

        // ensure not stuck paused
        {
            std::lock_guard<std::mutex> lk(mtx_);
            paused_.store(false);
        }
        cv_.notify_all();

        // allow subclasses to break blocking waits
        interrupt();

        // if never started, just mark exited and return
        if (!started_.load()) {
            {
                std::lock_guard<std::mutex> lk(exitMtx_);
                exited_.store(true);
            }
            exitCv_.notify_all();
            Logger::debug(name_ + " terminated.");
            return;
        }

        if (timeoutSec < 0.0) {
            waitForExit_();
        } else {
            waitForExit_(timeoutSec);
        }

        Logger::debug(name_ + " terminated.");
    }

protected:
    /**
     * @brief setup()
     *
     * Called exactly once by start(), before the worker thread begins.
     * Override this to initialize resources.
     *
     * To access user configuration, call configAs<T>() in derived classes.
     */
    virtual void setup() {}

    /**
     * @brief interrupt()
     *
     * Called by terminate() to allow derived classes to unblock any
     * blocking operations (e.g., close sockets/streams).
     */
    virtual void interrupt() {}

    /**
     * @brief process()
     *
     * Called repeatedly by the worker loop until terminate() is requested.
     */
    virtual void process() = 0;

    /**
     * @brief cleanup()
     *
     * Called once after the worker loop exits (termination requested).
     */
    virtual void cleanup() {}

    /**
     * @brief Access config as a concrete type.
     *
     * Throws if the stored config type does not match T.
     *
     * @tparam T expected config type
     * @return const reference to stored config
     */
    template <typename T>
    const T& configAs() const {
        if (!config_) {
            throw std::runtime_error(name_ + ": config is null");
        }
        if (configType_ != std::type_index(typeid(T))) {
            throw std::runtime_error(
                name_ + ": config type mismatch (stored=" + std::string(configType_.name()) +
                ", requested=" + std::string(typeid(T).name()) + ")"
            );
        }
        return *static_cast<const T*>(config_.get());
    }

private:
    void runLoop_() {
        Logger::debug(name_ + " started.");

        while (!terminating_.load()) {
            // pause gate
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this] { return !paused_.load() || terminating_.load(); });
            }

            if (terminating_.load()) {
                break;
            }

            process();
        }

        cleanup();

        {
            std::lock_guard<std::mutex> lk(exitMtx_);
            exited_.store(true);
        }
        exitCv_.notify_all();
    }

    void waitForExit_() {
        std::unique_lock<std::mutex> lk(exitMtx_);
        exitCv_.wait(lk, [this] { return exited_.load(); });
    }

    void waitForExit_(double timeoutSec) {
        using namespace std::chrono;
        std::unique_lock<std::mutex> lk(exitMtx_);
        exitCv_.wait_for(
            lk,
            duration_cast<milliseconds>(duration<double>(timeoutSec)),
            [this] { return exited_.load(); }
        );
    }

private:
    std::string name_;

    // type-erased config
    std::shared_ptr<void> config_;
    std::type_index       configType_{typeid(void)};

    std::thread worker_;

    // pause gate
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> paused_{false};

    // start/termination flags
    std::atomic<bool> started_{false};
    std::atomic<bool> terminating_{false};
    std::atomic<bool> terminated_{false};

    // exit notification
    std::mutex exitMtx_;
    std::condition_variable exitCv_;
    std::atomic<bool> exited_{false};
};

} // namespace magpie
