#pragma once

// macOS/BSD backend. Excluded from the build entirely on Linux via
// Makefile source selection.

#include "net/event_loop.h"
#include <unordered_map>

namespace net {

class KqueueReactor : public EventLoop {
public:
    KqueueReactor();
    ~KqueueReactor() override;

    bool add(int fd, uint8_t interestMask) override;
    bool modify(int fd, uint8_t interestMask) override;
    bool remove(int fd) override;
    int wait(std::vector<IoEvent>& out, int timeoutMs) override;

private:
    int kq_ = -1;
    // kqueue tracks read/write interest as separate filter registrations
    // rather than one combined mask like epoll, so we track the last
    // interest mask we asked for per fd in order to add/remove exactly
    // the filters that changed.
    std::unordered_map<int, uint8_t> interestByFd_;
};

} // namespace net
