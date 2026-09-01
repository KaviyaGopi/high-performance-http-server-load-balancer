#pragma once

#include "net/connection.h"
#include "net/event_loop.h"
#include "net/socket.h"
#include "net/thread_pool.h"
#include "router.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace server {

// Owns the reactor thread and the fixed worker thread pool. Exactly one
// reactor thread + N worker threads exist for the life of the process --
// no thread is ever created per connection or per request.
class HttpServer {
public:
    HttpServer(uint16_t port, size_t numThreads, Router router);
    ~HttpServer();

    // Starts the reactor thread and blocks the calling thread until
    // stop() is called (or SIGINT/SIGTERM via the caller's own handling).
    void run();
    void stop();

private:
    // Single-writer rule for the EventLoop and listening socket: only the
    // reactor thread (run()'s caller) ever calls add/modify/remove or
    // accept(). connections_ itself, however, is read by worker threads
    // (handleConnectionEvent) concurrently with inserts on the reactor
    // thread (handleAccept), so it is guarded by connectionsMutex_ --
    // std::unordered_map::insert can trigger a rehash that relocates the
    // whole bucket array, which would otherwise race with a concurrent
    // find() on any other fd, not just the one being inserted.
    void reactorLoop();
    void handleAccept();
    void applyPendingRearms();

    // Runs on a worker thread. Reads available bytes, parses, dispatches
    // to the router, writes the response, then posts a re-arm request
    // rather than touching the EventLoop directly.
    void handleConnectionEvent(int fd);

    enum class RearmAction { WaitReadable, WaitWritable, Close };
    struct PendingRearm {
        int fd;
        RearmAction action;
    };
    void postRearm(int fd, RearmAction action);

    uint16_t port_;
    Router router_;

    net::Socket listenSock_;
    std::unique_ptr<net::EventLoop> eventLoop_;
    net::ThreadPool pool_;

    std::mutex connectionsMutex_;
    std::unordered_map<int, std::unique_ptr<net::Connection>> connections_;

    std::mutex rearmMutex_;
    std::deque<PendingRearm> pendingRearms_;

    std::atomic<bool> running_{false};
};

} // namespace server
