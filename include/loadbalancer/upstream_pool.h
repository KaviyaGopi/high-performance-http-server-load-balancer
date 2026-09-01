#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace lb {

struct Upstream {
    std::string host;
    uint16_t port;
    std::atomic<bool> healthy{true};

    Upstream(std::string h, uint16_t p) : host(std::move(h)), port(p) {}
};

// Round-robins across the currently-healthy subset of a static upstream
// list using a lock-free atomic index.
class UpstreamPool {
public:
    explicit UpstreamPool(std::vector<std::pair<std::string, uint16_t>> targets);

    // std::atomic<size_t> has no move constructor, so this can't be
    // defaulted; the index simply restarts at 0 on move (fine -- it's
    // just a round-robin cursor, not observable state that must survive).
    UpstreamPool(UpstreamPool&& other) noexcept : upstreams_(std::move(other.upstreams_)) {}
    UpstreamPool& operator=(UpstreamPool&&) = delete;
    UpstreamPool(const UpstreamPool&) = delete;
    UpstreamPool& operator=(const UpstreamPool&) = delete;

    // Returns the next upstream in round-robin order, skipping unhealthy
    // ones. Returns nullptr only if every upstream is unhealthy.
    Upstream* pickNext();

    std::vector<std::unique_ptr<Upstream>>& all() { return upstreams_; }

private:
    std::vector<std::unique_ptr<Upstream>> upstreams_;
    std::atomic<size_t> nextIndex_{0};
};

} // namespace lb
