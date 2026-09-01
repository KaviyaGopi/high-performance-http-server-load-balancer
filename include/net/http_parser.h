#pragma once

#include "net/http_message.h"

namespace net {

enum class ParseStatus { NeedMoreData, Complete, Error };

// Incremental HTTP/1.1 REQUEST parser. Requests can straddle multiple
// non-blocking reads, so bytes are fed in via consume() and internal
// state persists between calls.
//
// In scope: request line, headers, Content-Length body, Connection:
// keep-alive/close with HTTP/1.0-vs-1.1 default semantics, malformed
// request rejection.
//
// Out of scope: chunked transfer-encoding, trailers, header folding,
// multiple in-flight pipelined requests per connection.
class HttpParser {
public:
    ParseStatus consume(const char* data, size_t len);

    bool done() const { return state_ == State::Complete; }
    bool failed() const { return state_ == State::Error; }
    const HttpRequest& request() const { return req_; }

    // Prepares the parser to parse the next request on the same
    // keep-alive connection.
    void reset();

private:
    enum class State { RequestLine, Headers, Body, Complete, Error };

    ParseStatus process();
    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    void finalizeHeaders();

    State state_ = State::RequestLine;
    std::string buffer_;
    size_t bodyBytesNeeded_ = 0;
    HttpRequest req_;
};

// Incremental HTTP/1.1 RESPONSE parser, used by the load balancer to read
// an upstream's response. Shares header/body parsing logic with
// HttpParser via free helper functions in the .cpp file.
class HttpResponseParser {
public:
    ParseStatus consume(const char* data, size_t len);

    bool done() const { return state_ == State::Complete; }
    bool failed() const { return state_ == State::Error; }
    const HttpResponse& response() const { return resp_; }

    void reset();

private:
    enum class State { StatusLine, Headers, Body, Complete, Error };

    ParseStatus process();
    bool parseStatusLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    void finalizeHeaders();

    State state_ = State::StatusLine;
    std::string buffer_;
    size_t bodyBytesNeeded_ = 0;
    HttpResponse resp_;
};

} // namespace net
