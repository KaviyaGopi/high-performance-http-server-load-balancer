#pragma once

// Linux-only backend. Excluded from the build entirely on non-Linux
// platforms via Makefile source selection.

#include "net/event_loop.h"

namespace net {

class EpollReactor : public EventLoop {
public:
    EpollReactor();
    ~EpollReactor() override;

    bool add(int fd, uint8_t interestMask) override;
    bool modify(int fd, uint8_t interestMask) override;
    bool remove(int fd) override;
    int wait(std::vector<IoEvent>& out, int timeoutMs) override;

private:
    int epollFd_ = -1;
};

} // namespace net
