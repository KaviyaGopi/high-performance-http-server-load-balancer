#include "net/socket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <unistd.h>

namespace net {

Socket::Socket(int fd) : fd_(fd) {}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int Socket::release() {
    int fd = fd_;
    fd_ = -1;
    return fd;
}

void Socket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool setTcpNoDelay(int fd) {
    int one = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0;
}

Socket createListenSocket(uint16_t port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return Socket();

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (!setNonBlocking(fd)) {
        ::close(fd);
        return Socket();
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return Socket();
    }
    if (listen(fd, backlog) != 0) {
        ::close(fd);
        return Socket();
    }
    return Socket(fd);
}

Socket connectNonBlocking(const char* host, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return Socket();

    if (!setNonBlocking(fd)) {
        ::close(fd);
        return Socket();
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ::close(fd);
        return Socket();
    }

    int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        ::close(fd);
        return Socket();
    }
    return Socket(fd);
}

AcceptResult acceptNonBlocking(int listenFd) {
    AcceptResult result;
    sockaddr_in peer{};
    socklen_t peerLen = sizeof(peer);

    int fd = accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            result.wouldBlock = true;
        }
        return result;
    }

    // macOS/BSD has no accept4(); the accepted fd starts blocking on every
    // platform here, so it must be made non-blocking explicitly before it
    // ever touches the reactor.
    if (!setNonBlocking(fd)) {
        ::close(fd);
        return result;
    }

    result.sock = Socket(fd);
    result.peer = peer;
    return result;
}

} // namespace net
