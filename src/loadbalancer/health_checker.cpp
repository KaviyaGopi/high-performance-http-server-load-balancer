#include "loadbalancer/health_checker.h"
#include "net/http_parser.h"
#include "net/socket.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace lb {

HealthChecker::HealthChecker(UpstreamPool& pool, int intervalMs) : pool_(pool), intervalMs_(intervalMs) {}

HealthChecker::~HealthChecker() { stop(); }

void HealthChecker::start() {
    running_ = true;
    thread_ = std::thread(&HealthChecker::loop, this);
}

void HealthChecker::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool HealthChecker::probeOnce(const Upstream& u) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500 * 1000; // 500ms
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(u.port);
    if (inet_pton(AF_INET, u.host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return false;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return false;
    }

    static const char* kRequest = "GET /health HTTP/1.1\r\nHost: healthcheck\r\nConnection: close\r\n\r\n";
    if (send(fd, kRequest, std::strlen(kRequest), 0) < 0) {
        close(fd);
        return false;
    }

    net::HttpResponseParser parser;
    char buf[2048];
    bool ok = false;
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        net::ParseStatus status = parser.consume(buf, static_cast<size_t>(n));
        if (status == net::ParseStatus::Complete) {
            ok = parser.response().status == 200;
            break;
        }
        if (status == net::ParseStatus::Error) break;
    }

    close(fd);
    return ok;
}

void HealthChecker::loop() {
    while (running_) {
        for (auto& u : pool_.all()) {
            bool ok = probeOnce(*u);
            bool wasHealthy = u->healthy.exchange(ok);
            if (wasHealthy != ok) {
                std::printf("healthcheck: %s:%u is now %s\n", u->host.c_str(), u->port, ok ? "UP" : "DOWN");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
    }
}

} // namespace lb
