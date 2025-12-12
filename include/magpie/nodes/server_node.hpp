#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <magpie/nodes/base_node.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/transport/rpc_responder.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {


/**
 * @brief ServerNode
 *
 * A node that:
 *  - receives RPC requests via an RpcResponder
 *  - dispatches each request to a handler in a worker thread
 *  - sends responses back from the BaseNode thread
 *
 * This allows multiple concurrent in-flight requests while keeping all
 * responder I/O in a single thread.
 *
 * Threading model:
 *  - BaseNode thread:
 *      - drains reply queue and sends responses
 *      - receives ONE request per tick (pollTimeoutSec) and dispatches it
 *  - Worker threads:
 *      - run handler(request) -> response
 *      - enqueue (response, clientCtx) into reply queue
 *
 * Notes:
 *  - All responder receive/send must happen on the BaseNode thread.
 *  - The handler runs concurrently on worker threads; it must be thread-safe.
 */
class ServerNode : public BaseNode {
public:
    using Object  = Value;
    using Handler = std::function<Object(const Object&)>;

    /**
     * @brief Construct a ServerNode.
     *
     * @param responder      Transport-level responder (e.g., ZmqRpcResponder).
     * @param handler        Function: handler(requestObj) -> responseObj.
     * @param maxWorkers     Number of worker threads running the handler.
     * @param pollTimeoutSec Small timeout used to poll for new requests in I/O loop.     
     * @param paused         Start paused if true.
     * @param name           Node name (used for logging/debugging).
     */
    ServerNode(std::shared_ptr<RpcResponder> responder,
               Handler handler,
               std::size_t maxWorkers = 4,
               double pollTimeoutSec = 0.01,
               bool paused = false,
               std::string name = "ServerNode")
        : BaseNode(EmptyConfig{}, paused, std::move(name))
        , responder_(std::move(responder))
        , handler_(std::move(handler))
        , maxWorkers_(maxWorkers)
        , pollTimeoutSec_(pollTimeoutSec)
    {}

    ~ServerNode() override = default;

    ServerNode(const ServerNode&)            = delete;
    ServerNode& operator=(const ServerNode&) = delete;

protected:
    /**
     * @brief setup()
     *
     * Builds a fixed-size worker pool.
     * Called once by BaseNode::start().
     *
     * If you later extend ServerNodeConfig, access it like:
     *   const auto& cfg = configAs<ServerNodeConfig>();
     */
    void setup() override {
        // (optional) touch cfg to validate config type early
        if (!responder_) {
            throw std::runtime_error(name() + ": responder is null");
        }
        if (!handler_) {
            throw std::runtime_error(name() + ": handler is empty");
        }
        if (maxWorkers_ == 0) {
            throw std::runtime_error(name() + ": maxWorkers must be > 0");
        }
        if (pollTimeoutSec_ <= 0.0) {
            throw std::runtime_error(name() + ": pollTimeoutSec must be > 0");
        }

        stopping_.store(false);

        // Start worker threads
        workers_.reserve(maxWorkers_);
        for (std::size_t i = 0; i < maxWorkers_; ++i) {
            workers_.emplace_back([this, i]() { workerLoop_(i); });
        }
    }

    /**
     * @brief process()
     *
     * One iteration of the I/O loop, called repeatedly by BaseNode.
     *
     * Mirrors your Python logic:
     *  1) Drain replies (send responses) first (low latency)
     *  2) Try to receive ONE request with pollTimeoutSec
     *  3) Dispatch to worker pool
     */
    void process() override {
        // 1) Send any ready responses first
        drainReplies_();

        // 2) Try receive a single request (small timeout)
        Object request;
        RpcResponder::ClientContext clientCtx;

        try {
            responder_->receive(request, clientCtx, pollTimeoutSec_);
        } catch (const TimeoutError&) {
            return; // no request within poll timeout
        } catch (const std::exception& e) {
            Logger::warning(name() + ": error receiving request: " + std::string(e.what()));
            return;
        }

        // 3) Dispatch handler to worker pool
        dispatchRequest_(std::move(request), std::move(clientCtx));
    }

    /**
     * @brief interrupt()
     *
     * Called when BaseNode::terminate() is requested.
     * We close the responder to unblock any pending receive().
     */
    void interrupt() override {
        try {
            if (responder_) responder_->close();
        } catch (...) {
            // best effort
        }

        // Wake any waiting worker threads
        {
            std::lock_guard<std::mutex> lk(jobMtx_);
            stopping_.store(true);
        }
        jobCv_.notify_all();
    }

    /**
     * @brief cleanup()
     *
     * Called after the BaseNode loop exits.
     * - stop workers (join)
     * - close responder
     */
    void cleanup() override {
        // Stop worker pool
        {
            std::lock_guard<std::mutex> lk(jobMtx_);
            stopping_.store(true);
        }
        jobCv_.notify_all();

        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();

        // Close responder
        try {
            if (responder_) responder_->close();
        } catch (const std::exception& e) {
            Logger::warning(name() + ": error closing responder: " + std::string(e.what()));
        }
    }

public:
    /**
     * @brief Terminate the server node (idempotent).
     *
     * Delegates to BaseNode::terminate() which triggers interrupt() and cleanup().
     */
    void terminate(double timeoutSec = -1.0) {
        BaseNode::terminate(timeoutSec);
    }

private:
    struct Job {
        Object request;
        RpcResponder::ClientContext clientCtx;
    };

    struct Reply {
        Object response;
        RpcResponder::ClientContext clientCtx;
    };

    void dispatchRequest_(Object request, RpcResponder::ClientContext clientCtx) {
        {
            std::lock_guard<std::mutex> lk(jobMtx_);
            jobs_.push(Job{std::move(request), std::move(clientCtx)});
        }
        jobCv_.notify_one();
    }

    void workerLoop_(std::size_t workerIndex) {
        (void)workerIndex;

        while (true) {
            Job job;

            // Wait for a job
            {
                std::unique_lock<std::mutex> lk(jobMtx_);
                jobCv_.wait(lk, [this] {
                    return stopping_.load() || !jobs_.empty();
                });

                if (stopping_.load() && jobs_.empty()) {
                    break;
                }

                job = std::move(jobs_.front());
                jobs_.pop();
            }

            // Run handler outside lock
            Object response;
            try {
                response = handler_(job.request);
            } catch (const std::exception& e) {
                Logger::warning(name() + ": handler error: " + std::string(e.what()));
                continue; // skip reply (same as your current behavior)
            } catch (...) {
                Logger::warning(name() + ": handler error: unknown");
                continue;
            }

            // Enqueue reply
            {
                std::lock_guard<std::mutex> lk(replyMtx_);
                replies_.push(Reply{std::move(response), std::move(job.clientCtx)});
            }
        }
    }

    void drainReplies_() {
        // Drain all currently queued replies; sending MUST happen on BaseNode thread.
        while (true) {
            Reply rep;
            {
                std::lock_guard<std::mutex> lk(replyMtx_);
                if (replies_.empty()) break;
                rep = std::move(replies_.front());
                replies_.pop();
            }

            try {
                responder_->send(rep.response, rep.clientCtx);
            } catch (const std::exception& e) {
                Logger::warning(name() + ": error sending response: " + std::string(e.what()));
            } catch (...) {
                Logger::warning(name() + ": error sending response: unknown");
            }
        }
    }

private:
    struct EmptyConfig {};
    
    std::shared_ptr<RpcResponder> responder_;
    Handler handler_;

    // Runtime parameters (NOT part of user config)
    std::size_t maxWorkers_{4};
    double      pollTimeoutSec_{0.01};

    // Worker pool
    std::vector<std::thread> workers_;
    std::atomic<bool> stopping_{false};

    // Job queue
    std::mutex jobMtx_;
    std::condition_variable jobCv_;
    std::queue<Job> jobs_;

    // Reply queue (worker -> BaseNode thread)
    std::mutex replyMtx_;
    std::queue<Reply> replies_;
};

} // namespace magpie
