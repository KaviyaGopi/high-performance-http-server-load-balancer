#include "net/epoll_reactor.h"

#include <sys/epoll.h>
#include <unistd.h>

namespace net {

namespace {

uint32_t toEpollEvents(uint8_t interestMask) {
    // Level-triggered by design: EPOLLET is deliberately never set so
    // behavior matches the kqueue backend (which is level-triggered
    // unless EV_CLEAR is passed).
    uint32_t events = 0;
    if (interestMask & EventType::Readable) events |= EPOLLIN;
    if (interestMask & EventType::Writable) events |= EPOLLOUT;
    return events;
}

uint8_t fromEpollEvents(uint32_t events) {
    uint8_t mask = 0;
    if (events & EPOLLIN) mask |= EventType::Readable;
    if (events & EPOLLOUT) mask |= EventType::Writable;
    return mask;
}

} // namespace

EpollReactor::EpollReactor() { epollFd_ = epoll_create1(0); }

EpollReactor::~EpollReactor() {
    if (epollFd_ >= 0) ::close(epollFd_);
}

bool EpollReactor::add(int fd, uint8_t interestMask) {
    epoll_event ev{};
    ev.events = toEpollEvents(interestMask);
    ev.data.fd = fd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == 0) return true;
    // Fall back to MOD in case fd was already registered (add-or-modify
    // semantics promised by the EventLoop interface).
    return epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EpollReactor::modify(int fd, uint8_t interestMask) {
    epoll_event ev{};
    ev.events = toEpollEvents(interestMask);
    ev.data.fd = fd;
    return epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EpollReactor::remove(int fd) {
    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EpollReactor::wait(std::vector<IoEvent>& out, int timeoutMs) {
    out.clear();
    epoll_event events[256];
    int n = epoll_wait(epollFd_, events, 256, timeoutMs);
    if (n <= 0) return n < 0 ? 0 : n;

    for (int i = 0; i < n; ++i) {
        IoEvent ev;
        ev.fd = events[i].data.fd;
        ev.mask = fromEpollEvents(events[i].events);
        ev.error = (events[i].events & (EPOLLERR | EPOLLHUP)) != 0;
        out.push_back(ev);
    }
    return static_cast<int>(out.size());
}

} // namespace net
