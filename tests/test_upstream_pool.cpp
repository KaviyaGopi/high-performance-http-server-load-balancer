#include "loadbalancer/upstream_pool.h"

#include <cstdio>

using lb::UpstreamPool;

namespace {

int failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    }
}

void testRoundRobinOrder() {
    UpstreamPool pool({{"127.0.0.1", 9001}, {"127.0.0.1", 9002}, {"127.0.0.1", 9003}});
    uint16_t p1 = pool.pickNext()->port;
    uint16_t p2 = pool.pickNext()->port;
    uint16_t p3 = pool.pickNext()->port;
    uint16_t p4 = pool.pickNext()->port;

    check(p1 != p2 && p2 != p3, "consecutive picks should differ across three upstreams");
    check(p4 == p1, "round robin should cycle back to the first upstream on the 4th pick");
}

void testSkipsUnhealthy() {
    UpstreamPool pool({{"127.0.0.1", 9001}, {"127.0.0.1", 9002}, {"127.0.0.1", 9003}});
    pool.all()[1]->healthy = false; // mark 9002 down

    for (int i = 0; i < 6; ++i) {
        lb::Upstream* u = pool.pickNext();
        check(u != nullptr, "pickNext should still return an upstream while others are healthy");
        check(u->port != 9002, "unhealthy upstream should never be selected");
    }
}

void testAllUnhealthyReturnsNull() {
    UpstreamPool pool({{"127.0.0.1", 9001}, {"127.0.0.1", 9002}});
    for (auto& u : pool.all()) u->healthy = false;
    check(pool.pickNext() == nullptr, "pickNext should return nullptr when every upstream is unhealthy");
}

} // namespace

int main() {
    testRoundRobinOrder();
    testSkipsUnhealthy();
    testAllUnhealthyReturnsNull();

    if (failures == 0) {
        std::printf("test_upstream_pool: all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_upstream_pool: %d failure(s)\n", failures);
    return 1;
}
