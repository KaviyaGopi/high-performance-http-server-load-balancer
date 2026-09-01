#include "net/connection.h"

#include <cerrno>
#include <sys/socket.h>

namespace net {

bool Connection::flushWrite() {
    while (writeOffset_ < writeBuffer_.size()) {
        ssize_t n = ::send(sock_.fd(), writeBuffer_.data() + writeOffset_,
                            writeBuffer_.size() - writeOffset_, 0);
        if (n > 0) {
            writeOffset_ += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false; // caller should wait for Writable and retry
        }
        // Real error or peer closed: treat remaining data as undeliverable.
        writeOffset_ = writeBuffer_.size();
        return true;
    }
    if (writeOffset_ == writeBuffer_.size() && !writeBuffer_.empty()) {
        writeBuffer_.clear();
        writeOffset_ = 0;
    }
    return true;
}

} // namespace net
