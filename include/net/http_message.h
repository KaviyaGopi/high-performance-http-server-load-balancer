#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace net {

using HeaderList = std::vector<std::pair<std::string, std::string>>;

const std::string* findHeader(const HeaderList& headers, std::string_view name);

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version; // "HTTP/1.1"
    HeaderList headers;
    std::string body;
    bool keepAlive = true;

    const std::string* header(std::string_view name) const { return findHeader(headers, name); }
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    HeaderList headers;
    std::string body;
    bool keepAlive = true;

    // Builds status line + headers (always including Content-Length and
    // Connection) + CRLF + body.
    std::string serialize() const;
};

HttpResponse makeJsonResponse(int status, const std::string& reason, const std::string& json);

} // namespace net
