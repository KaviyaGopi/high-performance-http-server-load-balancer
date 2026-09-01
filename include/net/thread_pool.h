#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace net {

// Fixed-size pool of worker threads created once at construction and
// joined at destruction. No thread is ever created or destroyed per task
// -- this is the mechanism behind "no thread thrashing": a burst of
// connections queues tasks rather than spawning threads.
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(std::function<void()> task);
    size_t size() const { return workers_.size(); }

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

} // namespace net
