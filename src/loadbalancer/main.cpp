#include "loadbalancer/load_balancer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

namespace {

// Signal handlers may only touch async-signal-safe state. Calling
// LoadBalancer::stop() directly here would run std::thread::join() (via
// HealthChecker::stop()) on the signal-handling stack; if a second
// SIGINT/SIGTERM arrives while that join() is still in flight, it
// re-enters the handler on the same thread and joins an already-joined
// thread, which throws std::system_error. Setting a flag and letting
// run()'s own loop notice it keeps all the real teardown work on the
// normal call stack. std::atomic (rather than volatile sig_atomic_t) so
// ThreadSanitizer can see the synchronization with the watcher thread.
std::atomic<bool> g_stopRequested{false};

void handleSignal(int) { g_stopRequested.store(true, std::memory_order_relaxed); }

std::vector<std::pair<std::string, uint16_t>> parseUpstreams(const std::string& csv) {
    std::vector<std::pair<std::string, uint16_t>> out;
    std::stringstream ss(csv);
    std::string entry;
    while (std::getline(ss, entry, ',')) {
        size_t colon = entry.find(':');
        if (colon == std::string::npos) continue;
        std::string host = entry.substr(0, colon);
        uint16_t port = static_cast<uint16_t>(std::atoi(entry.substr(colon + 1).c_str()));
        out.emplace_back(std::move(host), port);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    uint16_t port = 8080;
    size_t numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    int healthIntervalMs = 2000;
    std::string upstreamsCsv = "127.0.0.1:9001,127.0.0.1:9002,127.0.0.1:9003";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            numThreads = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--health-interval-ms") == 0 && i + 1 < argc) {
            healthIntervalMs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--upstreams") == 0 && i + 1 < argc) {
            upstreamsCsv = argv[++i];
        }
    }

    lb::UpstreamPool pool(parseUpstreams(upstreamsCsv));
    lb::LoadBalancer balancer(port, numThreads, std::move(pool), healthIntervalMs);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Polls the flag the signal handler sets and calls stop() from an
    // ordinary thread, so teardown (including HealthChecker's
    // std::thread::join()) never runs on a signal-handling stack.
    std::atomic<bool> watcherShouldExit{false};
    std::thread stopWatcher([&balancer, &watcherShouldExit] {
        while (!g_stopRequested.load(std::memory_order_relaxed) && !watcherShouldExit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (g_stopRequested.load(std::memory_order_relaxed)) balancer.stop();
    });

    balancer.run();
    watcherShouldExit = true;
    stopWatcher.join();
    return 0;
}
