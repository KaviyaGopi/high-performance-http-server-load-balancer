#include "http_server.h"
#include "router.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

// Signal handlers must stick to async-signal-safe operations. This flag
// is the only thing the handler touches; a small watcher thread (see
// main()) polls it and calls HttpServer::stop() from an ordinary call
// stack, so teardown never runs while a signal handler is on the stack.
// std::atomic<T>::store/load on a lock-free type is async-signal-safe in
// practice (used this way universally) and, unlike a plain volatile
// sig_atomic_t, is understood by ThreadSanitizer as synchronizing with
// the watcher thread's load.
std::atomic<bool> g_stopRequested{false};

void handleSignal(int) { g_stopRequested.store(true, std::memory_order_relaxed); }

uint16_t parsePort(const char* s) { return static_cast<uint16_t>(std::atoi(s)); }

} // namespace

int main(int argc, char** argv) {
    uint16_t port = 9001;
    size_t numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = parsePort(argv[++i]);
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            numThreads = static_cast<size_t>(std::atoi(argv[++i]));
        }
    }

    server::Router router;
    router.get("/health", [](const net::HttpRequest&) {
        return net::makeJsonResponse(200, "OK", R"({"status":"ok"})");
    });
    router.get("/", [port](const net::HttpRequest&) {
        std::string json = "{\"server\":\"" + std::to_string(port) +
                            "\",\"message\":\"hello from worker node\"}";
        return net::makeJsonResponse(200, "OK", json);
    });
    router.get("/api/echo", [port](const net::HttpRequest& req) {
        std::string json = "{\"server\":\"" + std::to_string(port) +
                            "\",\"target\":\"" + req.target + "\"}";
        return net::makeJsonResponse(200, "OK", json);
    });

    server::HttpServer httpServer(port, numThreads, std::move(router));
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::atomic<bool> watcherShouldExit{false};
    std::thread stopWatcher([&httpServer, &watcherShouldExit] {
        while (!g_stopRequested.load(std::memory_order_relaxed) && !watcherShouldExit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (g_stopRequested.load(std::memory_order_relaxed)) httpServer.stop();
    });

    httpServer.run();
    watcherShouldExit = true;
    stopWatcher.join();
    return 0;
}
