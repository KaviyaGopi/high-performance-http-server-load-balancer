#pragma once

#include "loadbalancer/upstream_pool.h"

#include <atomic>
#include <thread>

namespace lb {

// Active health checking on one dedicated background thread (not the
// reactor, not the worker pool -- this is low-frequency, I/O-bound, and
// orthogonal to the request-serving hot path). Polls each upstream's
// /health endpoint on an interval and flips Upstream::healthy.
class HealthChecker {
public:
    HealthChecker(UpstreamPool& pool, int intervalMs);
    ~HealthChecker();

    void start();
    void stop();

private:
    void loop();
    static bool probeOnce(const Upstream& u);

    UpstreamPool& pool_;
    int intervalMs_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace lb
