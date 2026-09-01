#pragma once

#include <cstdint>
#include <netinet/in.h>

namespace net {

// RAII wrapper over a raw file descriptor. Move-only so a fd is always
// owned by exactly one Socket and closed exactly once.
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    int fd() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

    // Relinquish ownership of the fd without closing it; caller becomes
    // responsible for it.
    int release();

    void close();

private:
    int fd_ = -1;
};

// Builds a non-blocking, SO_REUSEADDR listening socket bound to
// 0.0.0.0:port with the given backlog. Returns an invalid Socket on
// failure (caller should check valid()).
Socket createListenSocket(uint16_t port, int backlog = 1024);

// Opens a non-blocking TCP connection to host:port. Since the socket is
// non-blocking, connect() will typically return EINPROGRESS; the caller
// must wait for the fd to become writable to know when the connection
// completes (or fails).
Socket connectNonBlocking(const char* host, uint16_t port);

bool setNonBlocking(int fd);
bool setTcpNoDelay(int fd);

struct AcceptResult {
    Socket sock;
    sockaddr_in peer{};
    bool wouldBlock = false;
};

// Accepts one pending connection off a non-blocking listen socket. If no
// connection is pending, returns a result with wouldBlock = true and an
// invalid Socket.
AcceptResult acceptNonBlocking(int listenFd);

} // namespace net
