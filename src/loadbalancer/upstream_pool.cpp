#include "loadbalancer/upstream_pool.h"

namespace lb {

UpstreamPool::UpstreamPool(std::vector<std::pair<std::string, uint16_t>> targets) {
    upstreams_.reserve(targets.size());
    for (auto& [host, port] : targets) {
        upstreams_.push_back(std::make_unique<Upstream>(host, port));
    }
}

Upstream* UpstreamPool::pickNext() {
    size_t count = upstreams_.size();
    if (count == 0) return nullptr;

    size_t start = nextIndex_.fetch_add(1, std::memory_order_relaxed) % count;
    for (size_t i = 0; i < count; ++i) {
        Upstream* candidate = upstreams_[(start + i) % count].get();
        if (candidate->healthy.load(std::memory_order_relaxed)) {
            return candidate;
        }
    }
    return nullptr;
}

} // namespace lb
