#include "net/event_loop.h"

#if defined(__linux__)
#include "net/epoll_reactor.h"
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include "net/kqueue_reactor.h"
#else
#error "No supported I/O multiplexing backend for this platform"
#endif

namespace net {

std::unique_ptr<EventLoop> EventLoop::create() {
#if defined(__linux__)
    return std::make_unique<EpollReactor>();
#else
    return std::make_unique<KqueueReactor>();
#endif
}

} // namespace net
