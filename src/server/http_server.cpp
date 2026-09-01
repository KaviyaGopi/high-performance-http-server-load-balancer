#include "http_server.h"

#include <cerrno>
#include <cstdio>
#include <sys/socket.h>

namespace server {

HttpServer::HttpServer(uint16_t port, size_t numThreads, Router router)
    : port_(port), router_(std::move(router)), pool_(numThreads) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::stop() { running_ = false; }

void HttpServer::run() {
    listenSock_ = net::createListenSocket(port_);
    if (!listenSock_.valid()) {
        std::fprintf(stderr, "server: failed to bind port %u\n", port_);
        return;
    }

    eventLoop_ = net::EventLoop::create();
    eventLoop_->add(listenSock_.fd(), net::EventType::Readable);

    running_ = true;
    std::printf("server: listening on 0.0.0.0:%u with %zu worker threads\n", port_, pool_.size());
    reactorLoop();
}

void HttpServer::reactorLoop() {
    std::vector<net::IoEvent> events;
    while (running_) {
        applyPendingRearms();

        int n = eventLoop_->wait(events, 100 /* ms */);
        if (n <= 0) continue;

        for (auto& ev : events) {
            if (ev.fd == listenSock_.fd()) {
                handleAccept();
                continue;
            }

            bool found;
            {
                std::lock_guard<std::mutex> lock(connectionsMutex_);
                found = connections_.count(ev.fd) != 0;
            }
            if (!found) continue;

            if (ev.error) {
                eventLoop_->remove(ev.fd);
                std::lock_guard<std::mutex> lock(connectionsMutex_);
                connections_.erase(ev.fd);
                continue;
            }

            // Disable further readiness delivery for this fd immediately,
            // before handing it to a worker thread. Without this, a
            // level-triggered fd that is still readable/writable when the
            // reactor wakes again would be re-enqueued while a worker is
            // still mid-read/write on it -- the classic duplicate-dispatch
            // bug. The worker re-arms interest itself once it's done via
            // postRearm(), which the reactor thread applies at the top of
            // the next loop iteration.
            eventLoop_->modify(ev.fd, 0);

            int fd = ev.fd;
            pool_.enqueue([this, fd] { handleConnectionEvent(fd); });
        }
    }
}

void HttpServer::handleAccept() {
    for (;;) {
        net::AcceptResult res = net::acceptNonBlocking(listenSock_.fd());
        if (res.wouldBlock) break;
        if (!res.sock.valid()) break;

        int fd = res.sock.fd();
        net::setTcpNoDelay(fd);
        auto conn = std::make_unique<net::Connection>(std::move(res.sock));
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            connections_[fd] = std::move(conn);
        }
        eventLoop_->add(fd, net::EventType::Readable);
    }
}

void HttpServer::postRearm(int fd, RearmAction action) {
    std::lock_guard<std::mutex> lock(rearmMutex_);
    pendingRearms_.push_back({fd, action});
}

void HttpServer::applyPendingRearms() {
    std::deque<PendingRearm> rearms;
    {
        std::lock_guard<std::mutex> lock(rearmMutex_);
        rearms.swap(pendingRearms_);
    }
    for (auto& r : rearms) {
        bool found;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            found = connections_.count(r.fd) != 0;
        }
        if (!found) continue;

        switch (r.action) {
            case RearmAction::WaitReadable:
                eventLoop_->modify(r.fd, net::EventType::Readable);
                break;
            case RearmAction::WaitWritable:
                eventLoop_->modify(r.fd, net::EventType::Writable);
                break;
            case RearmAction::Close:
                eventLoop_->remove(r.fd);
                std::lock_guard<std::mutex> lock(connectionsMutex_);
                connections_.erase(r.fd);
                break;
        }
    }
}

void HttpServer::handleConnectionEvent(int fd) {
    // The map lookup itself needs connectionsMutex_ (insert on the reactor
    // thread can rehash the table concurrently with this find()). The
    // returned Connection* is then safe to use lock-free: the reactor
    // thread only erases this fd's entry via applyPendingRearms() after
    // this same worker has posted that Close action for it, so the
    // Connection object itself outlives this call.
    net::Connection* conn;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;
        conn = it->second.get();
    }

    if (conn->hasPendingWrite()) {
        if (!conn->flushWrite()) {
            postRearm(fd, RearmAction::WaitWritable);
            return;
        }
        conn->parser().reset();
        postRearm(fd, conn->pendingKeepAlive() ? RearmAction::WaitReadable : RearmAction::Close);
        return;
    } else {
        char buf[8192];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            postRearm(fd, RearmAction::Close);
            return;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                postRearm(fd, RearmAction::WaitReadable);
                return;
            }
            postRearm(fd, RearmAction::Close);
            return;
        }

        net::ParseStatus status = conn->parser().consume(buf, static_cast<size_t>(n));
        if (status == net::ParseStatus::NeedMoreData) {
            postRearm(fd, RearmAction::WaitReadable);
            return;
        }
        if (status == net::ParseStatus::Error) {
            net::HttpResponse resp = net::makeJsonResponse(400, "Bad Request", R"({"error":"bad request"})");
            resp.keepAlive = false;
            conn->setPendingKeepAlive(false);
            conn->queueWrite(resp.serialize());
            if (!conn->flushWrite()) {
                postRearm(fd, RearmAction::WaitWritable);
            } else {
                postRearm(fd, RearmAction::Close);
            }
            return;
        }

        const net::HttpRequest& req = conn->parser().request();
        net::HttpResponse resp = router_.dispatch(req);
        resp.keepAlive = req.keepAlive;
        conn->setPendingKeepAlive(req.keepAlive);
        conn->queueWrite(resp.serialize());

        if (!conn->flushWrite()) {
            postRearm(fd, RearmAction::WaitWritable);
            return;
        }
        conn->parser().reset();
        postRearm(fd, req.keepAlive ? RearmAction::WaitReadable : RearmAction::Close);
        return;
    }
}

} // namespace server
