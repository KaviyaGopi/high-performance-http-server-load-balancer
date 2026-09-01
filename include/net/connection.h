#pragma once

#include "net/http_parser.h"
#include "net/socket.h"

namespace net {

// Per-fd state that must survive across reactor-thread/worker-thread
// handoffs: the socket itself, the in-progress request parser, and any
// unflushed response bytes (for when write() would otherwise block).
class Connection {
public:
    explicit Connection(Socket sock) : sock_(std::move(sock)) {}

    int fd() const { return sock_.fd(); }
    Socket& socket() { return sock_; }
    HttpParser& parser() { return parser_; }

    void queueWrite(std::string data) {
        writeBuffer_ += std::move(data);
    }

    // Attempts to flush as much of writeBuffer_ as the socket will
    // accept without blocking. Returns true once fully flushed.
    bool flushWrite();

    bool hasPendingWrite() const { return writeOffset_ < writeBuffer_.size(); }

    // Whether the response currently queued/flushing should keep the
    // connection open once fully written. Set alongside queueWrite() so a
    // worker resuming a partially-flushed write (from a later event) still
    // knows how to re-arm once it finishes.
    void setPendingKeepAlive(bool keepAlive) { pendingKeepAlive_ = keepAlive; }
    bool pendingKeepAlive() const { return pendingKeepAlive_; }

private:
    Socket sock_;
    HttpParser parser_;
    std::string writeBuffer_;
    size_t writeOffset_ = 0;
    bool pendingKeepAlive_ = true;
};

} // namespace net
