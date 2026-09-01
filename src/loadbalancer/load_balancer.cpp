#include "loadbalancer/load_balancer.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

namespace lb {

LoadBalancer::LoadBalancer(uint16_t port, size_t numThreads, UpstreamPool pool, int healthIntervalMs)
    : port_(port), upstreamPool_(std::move(pool)), healthChecker_(upstreamPool_, healthIntervalMs), pool_(numThreads) {}

LoadBalancer::~LoadBalancer() { stop(); }

void LoadBalancer::stop() {
    running_ = false;
    healthChecker_.stop();
}

void LoadBalancer::run() {
    listenSock_ = net::createListenSocket(port_);
    if (!listenSock_.valid()) {
        std::fprintf(stderr, "loadbalancer: failed to bind port %u\n", port_);
        return;
    }

    eventLoop_ = net::EventLoop::create();
    eventLoop_->add(listenSock_.fd(), net::EventType::Readable);

    healthChecker_.start();

    running_ = true;
    std::printf("loadbalancer: listening on 0.0.0.0:%u with %zu worker threads, %zu upstreams\n",
                port_, pool_.size(), upstreamPool_.all().size());
    reactorLoop();
}

void LoadBalancer::reactorLoop() {
    std::vector<net::IoEvent> events;
    while (running_) {
        applyPendingRearms();

        int n = eventLoop_->wait(events, 100);
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

            eventLoop_->modify(ev.fd, 0);
            int fd = ev.fd;
            pool_.enqueue([this, fd] { handleClientEvent(fd); });
        }
    }
}

void LoadBalancer::handleAccept() {
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

void LoadBalancer::postRearm(int fd, RearmAction action) {
    std::lock_guard<std::mutex> lock(rearmMutex_);
    pendingRearms_.push_back({fd, action});
}

void LoadBalancer::applyPendingRearms() {
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

namespace {

// Sends the full buffer on a blocking (or timeout-bound) socket.
bool sendAll(int fd, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t n = ::send(fd, data.data() + offset, data.size() - offset, 0);
        if (n <= 0) return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

bool LoadBalancer::proxyRequest(net::Connection& conn, const net::HttpRequest& req) {
    Upstream* upstream = upstreamPool_.pickNext();
    if (!upstream) {
        net::HttpResponse resp = net::makeJsonResponse(502, "Bad Gateway", R"({"error":"no healthy upstream"})");
        resp.keepAlive = false;
        conn.setPendingKeepAlive(false);
        conn.queueWrite(resp.serialize());
        return false;
    }

    // MVP simplification: a fresh short-lived, timeout-bound connection is
    // opened to the upstream per proxied request rather than pooling
    // upstream connections. This keeps correctness simple while still
    // fully demonstrating round-robin routing and reverse proxying.
    int ufd = socket(AF_INET, SOCK_STREAM, 0);
    if (ufd < 0) {
        net::HttpResponse resp = net::makeJsonResponse(502, "Bad Gateway", R"({"error":"upstream connect failed"})");
        resp.keepAlive = false;
        conn.setPendingKeepAlive(false);
        conn.queueWrite(resp.serialize());
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(ufd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(ufd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(upstream->port);
    inet_pton(AF_INET, upstream->host.c_str(), &addr.sin_addr);

    bool connected = ::connect(ufd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;

    net::HttpResponse resp;
    bool ok = false;
    if (connected) {
        // Forward the request largely as raw bytes (reconstructed via
        // HttpRequest fields, since we only buffered the parsed form) with
        // Connection: close to the upstream, since we don't pool upstream
        // connections.
        std::string out = req.method + " " + req.target + " " + req.version + "\r\n";
        bool sawConnection = false;
        for (auto& [k, v] : req.headers) {
            if (strcasecmp(k.c_str(), "connection") == 0) {
                sawConnection = true;
                out += "Connection: close\r\n";
                continue;
            }
            out += k + ": " + v + "\r\n";
        }
        if (!sawConnection) out += "Connection: close\r\n";
        out += "\r\n";
        out += req.body;

        if (sendAll(ufd, out)) {
            net::HttpResponseParser parser;
            char buf[8192];
            for (;;) {
                ssize_t n = ::recv(ufd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                net::ParseStatus status = parser.consume(buf, static_cast<size_t>(n));
                if (status == net::ParseStatus::Complete) {
                    resp = parser.response();
                    ok = true;
                    break;
                }
                if (status == net::ParseStatus::Error) break;
            }
        }
    }
    close(ufd);

    if (!ok) {
        resp = net::makeJsonResponse(502, "Bad Gateway", R"({"error":"upstream request failed"})");
    }
    resp.keepAlive = req.keepAlive;
    conn.setPendingKeepAlive(req.keepAlive);
    conn.queueWrite(resp.serialize());
    return ok;
}

void LoadBalancer::handleClientEvent(int fd) {
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
    }

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
    proxyRequest(*conn, req);

    if (!conn->flushWrite()) {
        postRearm(fd, RearmAction::WaitWritable);
        return;
    }
    conn->parser().reset();
    postRearm(fd, conn->pendingKeepAlive() ? RearmAction::WaitReadable : RearmAction::Close);
}

} // namespace lb
