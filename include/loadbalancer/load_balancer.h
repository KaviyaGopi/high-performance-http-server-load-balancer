#pragma once

#include "loadbalancer/health_checker.h"
#include "loadbalancer/upstream_pool.h"
#include "net/connection.h"
#include "net/event_loop.h"
#include "net/socket.h"
#include "net/thread_pool.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace lb {

// Reverse proxy / load balancer. Reuses the same 1-reactor-thread +
// fixed-worker-pool architecture as the HTTP server (net::EventLoop,
// net::ThreadPool) so the codebase has one consistent concurrency story.
//
// Per proxied request, a worker opens a new short-lived connection to the
// chosen upstream (no upstream connection pooling in this MVP -- a named
// simplification, not an oversight) and relays bytes in both directions.
class LoadBalancer {
public:
    LoadBalancer(uint16_t port, size_t numThreads, UpstreamPool pool, int healthIntervalMs);
    ~LoadBalancer();

    void run();
    void stop();

private:
    // connections_ is read by worker threads (handleClientEvent)
    // concurrently with inserts on the reactor thread (handleAccept), so
    // it is guarded by connectionsMutex_ -- see the identical note on
    // server::HttpServer for why a plain unordered_map isn't safe here
    // (insert-triggered rehash can race with a concurrent find()).
    void reactorLoop();
    void handleAccept();
    void applyPendingRearms();
    void handleClientEvent(int fd);
    bool proxyRequest(net::Connection& conn, const net::HttpRequest& req);

    enum class RearmAction { WaitReadable, WaitWritable, Close };
    struct PendingRearm {
        int fd;
        RearmAction action;
    };
    void postRearm(int fd, RearmAction action);

    uint16_t port_;
    UpstreamPool upstreamPool_;
    HealthChecker healthChecker_;

    net::Socket listenSock_;
    std::unique_ptr<net::EventLoop> eventLoop_;
    net::ThreadPool pool_;

    std::mutex connectionsMutex_;
    std::unordered_map<int, std::unique_ptr<net::Connection>> connections_;

    std::mutex rearmMutex_;
    std::deque<PendingRearm> pendingRearms_;

    std::atomic<bool> running_{false};
};

} // namespace lb
