#include "net/kqueue_reactor.h"

#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace net {

KqueueReactor::KqueueReactor() { kq_ = kqueue(); }

KqueueReactor::~KqueueReactor() {
    if (kq_ >= 0) ::close(kq_);
}

bool KqueueReactor::add(int fd, uint8_t interestMask) {
    return modify(fd, interestMask);
}

bool KqueueReactor::modify(int fd, uint8_t interestMask) {
    uint8_t prev = 0;
    auto it = interestByFd_.find(fd);
    if (it != interestByFd_.end()) prev = it->second;

    std::vector<struct kevent> changes;
    changes.reserve(2);

    bool wantRead = interestMask & EventType::Readable;
    bool hadRead = prev & EventType::Readable;
    if (wantRead != hadRead) {
        struct kevent kev;
        // Level-triggered: EV_CLEAR is deliberately never passed, so
        // semantics match the epoll backend (which never sets EPOLLET).
        EV_SET(&kev, fd, EVFILT_READ, wantRead ? EV_ADD : EV_DELETE, 0, 0, nullptr);
        changes.push_back(kev);
    }

    bool wantWrite = interestMask & EventType::Writable;
    bool hadWrite = prev & EventType::Writable;
    if (wantWrite != hadWrite) {
        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_WRITE, wantWrite ? EV_ADD : EV_DELETE, 0, 0, nullptr);
        changes.push_back(kev);
    }

    if (!changes.empty()) {
        if (kevent(kq_, changes.data(), static_cast<int>(changes.size()), nullptr, 0, nullptr) != 0) {
            return false;
        }
    }

    if (interestMask == 0) {
        interestByFd_.erase(fd);
    } else {
        interestByFd_[fd] = interestMask;
    }
    return true;
}

bool KqueueReactor::remove(int fd) {
    auto it = interestByFd_.find(fd);
    uint8_t prev = (it != interestByFd_.end()) ? it->second : 0;

    struct kevent changes[2];
    int n = 0;
    if (prev & EventType::Readable) {
        EV_SET(&changes[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    }
    if (prev & EventType::Writable) {
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    if (n > 0) {
        // Ignore errors here: the fd may already be closed, which
        // implicitly drops its kqueue registrations.
        kevent(kq_, changes, n, nullptr, 0, nullptr);
    }
    interestByFd_.erase(fd);
    return true;
}

int KqueueReactor::wait(std::vector<IoEvent>& out, int timeoutMs) {
    out.clear();
    struct kevent events[256];

    struct timespec ts;
    struct timespec* tsPtr = nullptr;
    if (timeoutMs >= 0) {
        ts.tv_sec = timeoutMs / 1000;
        ts.tv_nsec = (timeoutMs % 1000) * 1000000L;
        tsPtr = &ts;
    }

    int n = kevent(kq_, nullptr, 0, events, 256, tsPtr);
    if (n <= 0) return n < 0 ? 0 : n;

    // Two kevent entries (read + write) can arrive for the same fd in one
    // call; merge them into a single IoEvent per fd like epoll would.
    std::unordered_map<int, IoEvent> merged;
    for (int i = 0; i < n; ++i) {
        int fd = static_cast<int>(events[i].ident);
        IoEvent& ev = merged[fd];
        ev.fd = fd;
        if (events[i].filter == EVFILT_READ) ev.mask |= EventType::Readable;
        if (events[i].filter == EVFILT_WRITE) ev.mask |= EventType::Writable;
        if (events[i].flags & (EV_ERROR | EV_EOF)) ev.error = true;
    }

    for (auto& [fd, ev] : merged) {
        out.push_back(ev);
    }
    return static_cast<int>(out.size());
}

} // namespace net
