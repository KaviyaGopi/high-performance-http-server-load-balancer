#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace net {

enum EventType : uint8_t {
    Readable = 1u << 0,
    Writable = 1u << 1,
};

struct IoEvent {
    int fd = -1;
    uint8_t mask = 0;   // bitwise-OR of EventType flags that fired
    bool error = false; // hangup/error condition on this fd
};

// Abstract I/O multiplexer. Implementations are level-triggered on both
// backends (epoll on Linux, kqueue on macOS/BSD) so callers see identical
// semantics regardless of platform. Not thread-safe by contract: exactly
// one thread (the reactor thread) is expected to call these methods.
class EventLoop {
public:
    virtual ~EventLoop() = default;

    // Registers fd with the given interest mask. Safe to call on an
    // already-registered fd to change its interest set (add-or-modify).
    virtual bool add(int fd, uint8_t interestMask) = 0;
    virtual bool modify(int fd, uint8_t interestMask) = 0;
    virtual bool remove(int fd) = 0;

    // Blocks up to timeoutMs (-1 = forever) for ready fds. Fills `out`
    // (cleared first) and returns the number of ready events.
    virtual int wait(std::vector<IoEvent>& out, int timeoutMs) = 0;

    // Selects the compiled-in backend for this platform.
    static std::unique_ptr<EventLoop> create();
};

} // namespace net
