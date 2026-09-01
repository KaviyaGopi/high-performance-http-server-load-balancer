#include "net/thread_pool.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>

using net::ThreadPool;

namespace {

int failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    }
}

void testAllTasksRun() {
    constexpr size_t kPoolSize = 4;
    constexpr int kTaskCount = 200;

    ThreadPool pool(kPoolSize);
    std::atomic<int> counter{0};
    std::mutex idsMutex;
    std::set<std::thread::id> observedIds;

    for (int i = 0; i < kTaskCount; ++i) {
        pool.enqueue([&] {
            counter.fetch_add(1);
            std::lock_guard<std::mutex> lock(idsMutex);
            observedIds.insert(std::this_thread::get_id());
        });
    }

    // Give tasks time to drain; ThreadPool's destructor also joins, but we
    // want to assert before it's torn down.
    for (int i = 0; i < 100 && counter.load() < kTaskCount; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    check(counter.load() == kTaskCount, "every enqueued task should have run exactly once");
    check(observedIds.size() <= kPoolSize, "no more distinct threads than the fixed pool size should ever run tasks");
    check(pool.size() == kPoolSize, "pool size should match constructor argument");
}

} // namespace

int main() {
    testAllTasksRun();

    if (failures == 0) {
        std::printf("test_thread_pool: all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_thread_pool: %d failure(s)\n", failures);
    return 1;
}
