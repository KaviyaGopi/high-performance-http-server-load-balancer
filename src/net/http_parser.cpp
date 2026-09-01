#include "net/http_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace net {

namespace {

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string trim(std::string_view s) {
    size_t start = 0, end = s.size();
    while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return std::string(s.substr(start, end - start));
}

// Extracts one CRLF-terminated line from buffer starting at pos. Returns
// true and advances pos past the line (including the CRLF) on success.
bool extractLine(const std::string& buffer, size_t& pos, std::string& line) {
    size_t crlf = buffer.find("\r\n", pos);
    if (crlf == std::string::npos) return false;
    line = buffer.substr(pos, crlf - pos);
    pos = crlf + 2;
    return true;
}

// Parses one "Name: value" header line into the list, and returns the
// Content-Length if that header was present (nullopt-style via bool).
bool parseGenericHeaderLine(const std::string& line, HeaderList& headers) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::string name = trim(std::string_view(line).substr(0, colon));
    std::string value = trim(std::string_view(line).substr(colon + 1));
    if (name.empty()) return false;
    headers.emplace_back(std::move(name), std::move(value));
    return true;
}

size_t contentLengthFrom(const HeaderList& headers) {
    const std::string* cl = findHeader(headers, "Content-Length");
    if (!cl) return 0;
    char* end = nullptr;
    long v = std::strtol(cl->c_str(), &end, 10);
    if (end == cl->c_str() || v < 0) return 0;
    return static_cast<size_t>(v);
}

bool connectionKeepAlive(const HeaderList& headers, std::string_view version) {
    const std::string* conn = findHeader(headers, "Connection");
    if (conn) {
        std::string v = toLower(*conn);
        if (v.find("close") != std::string::npos) return false;
        if (v.find("keep-alive") != std::string::npos) return true;
    }
    // HTTP/1.1 defaults to keep-alive; HTTP/1.0 defaults to close.
    return version == "HTTP/1.1";
}

} // namespace

const std::string* findHeader(const HeaderList& headers, std::string_view name) {
    for (auto& [k, v] : headers) {
        if (k.size() == name.size() &&
            std::equal(k.begin(), k.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
            return &v;
        }
    }
    return nullptr;
}

std::string HttpResponse::serialize() const {
    std::string out;
    out += "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    bool hasContentLength = false;
    bool hasConnection = false;
    for (auto& [k, v] : headers) {
        if (toLower(k) == "content-length") hasContentLength = true;
        if (toLower(k) == "connection") hasConnection = true;
        out += k + ": " + v + "\r\n";
    }
    if (!hasContentLength) {
        out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    if (!hasConnection) {
        out += std::string("Connection: ") + (keepAlive ? "keep-alive" : "close") + "\r\n";
    }
    out += "\r\n";
    out += body;
    return out;
}

HttpResponse makeJsonResponse(int status, const std::string& reason, const std::string& json) {
    HttpResponse resp;
    resp.status = status;
    resp.reason = reason;
    resp.headers.emplace_back("Content-Type", "application/json");
    resp.body = json;
    return resp;
}

// ---------------------------------------------------------------------
// HttpParser (requests)
// ---------------------------------------------------------------------

void HttpParser::reset() {
    state_ = State::RequestLine;
    buffer_.clear();
    bodyBytesNeeded_ = 0;
    req_ = HttpRequest();
}

bool HttpParser::parseRequestLine(const std::string& line) {
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return false;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;

    req_.method = line.substr(0, sp1);
    req_.target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    req_.version = line.substr(sp2 + 1);

    if (req_.method.empty() || req_.target.empty()) return false;
    if (req_.version != "HTTP/1.1" && req_.version != "HTTP/1.0") return false;
    return true;
}

bool HttpParser::parseHeaderLine(const std::string& line) {
    return parseGenericHeaderLine(line, req_.headers);
}

void HttpParser::finalizeHeaders() {
    bodyBytesNeeded_ = contentLengthFrom(req_.headers);
    req_.keepAlive = connectionKeepAlive(req_.headers, req_.version);
}

ParseStatus HttpParser::consume(const char* data, size_t len) {
    buffer_.append(data, len);
    return process();
}

ParseStatus HttpParser::process() {
    size_t pos = 0;

    if (state_ == State::RequestLine) {
        std::string line;
        if (!extractLine(buffer_, pos, line)) return ParseStatus::NeedMoreData;
        if (!parseRequestLine(line)) {
            state_ = State::Error;
            return ParseStatus::Error;
        }
        buffer_.erase(0, pos);
        pos = 0;
        state_ = State::Headers;
    }

    if (state_ == State::Headers) {
        for (;;) {
            std::string line;
            if (!extractLine(buffer_, pos, line)) return ParseStatus::NeedMoreData;
            if (line.empty()) {
                buffer_.erase(0, pos);
                pos = 0;
                finalizeHeaders();
                state_ = State::Body;
                break;
            }
            if (!parseHeaderLine(line)) {
                state_ = State::Error;
                return ParseStatus::Error;
            }
            buffer_.erase(0, pos);
            pos = 0;
        }
    }

    if (state_ == State::Body) {
        if (buffer_.size() < bodyBytesNeeded_) return ParseStatus::NeedMoreData;
        req_.body = buffer_.substr(0, bodyBytesNeeded_);
        buffer_.erase(0, bodyBytesNeeded_);
        state_ = State::Complete;
    }

    return ParseStatus::Complete;
}

// ---------------------------------------------------------------------
// HttpResponseParser (used by the load balancer to read upstream responses)
// ---------------------------------------------------------------------

void HttpResponseParser::reset() {
    state_ = State::StatusLine;
    buffer_.clear();
    bodyBytesNeeded_ = 0;
    resp_ = HttpResponse();
}

bool HttpResponseParser::parseStatusLine(const std::string& line) {
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return false;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) sp2 = line.size();

    std::string version = line.substr(0, sp1);
    if (version != "HTTP/1.1" && version != "HTTP/1.0") return false;

    std::string statusStr = line.substr(sp1 + 1, sp2 - sp1 - 1);
    char* end = nullptr;
    long status = std::strtol(statusStr.c_str(), &end, 10);
    if (end == statusStr.c_str()) return false;

    resp_.status = static_cast<int>(status);
    resp_.reason = (sp2 < line.size()) ? line.substr(sp2 + 1) : "";
    return true;
}

bool HttpResponseParser::parseHeaderLine(const std::string& line) {
    return parseGenericHeaderLine(line, resp_.headers);
}

void HttpResponseParser::finalizeHeaders() {
    bodyBytesNeeded_ = contentLengthFrom(resp_.headers);
    resp_.keepAlive = connectionKeepAlive(resp_.headers, "HTTP/1.1");
}

ParseStatus HttpResponseParser::consume(const char* data, size_t len) {
    buffer_.append(data, len);
    return process();
}

ParseStatus HttpResponseParser::process() {
    size_t pos = 0;

    if (state_ == State::StatusLine) {
        std::string line;
        if (!extractLine(buffer_, pos, line)) return ParseStatus::NeedMoreData;
        if (!parseStatusLine(line)) {
            state_ = State::Error;
            return ParseStatus::Error;
        }
        buffer_.erase(0, pos);
        pos = 0;
        state_ = State::Headers;
    }

    if (state_ == State::Headers) {
        for (;;) {
            std::string line;
            if (!extractLine(buffer_, pos, line)) return ParseStatus::NeedMoreData;
            if (line.empty()) {
                buffer_.erase(0, pos);
                pos = 0;
                finalizeHeaders();
                state_ = State::Body;
                break;
            }
            if (!parseHeaderLine(line)) {
                state_ = State::Error;
                return ParseStatus::Error;
            }
            buffer_.erase(0, pos);
            pos = 0;
        }
    }

    if (state_ == State::Body) {
        if (buffer_.size() < bodyBytesNeeded_) return ParseStatus::NeedMoreData;
        resp_.body = buffer_.substr(0, bodyBytesNeeded_);
        buffer_.erase(0, bodyBytesNeeded_);
        state_ = State::Complete;
    }

    return ParseStatus::Complete;
}

} // namespace net
