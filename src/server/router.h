#pragma once

#include "net/http_message.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace server {

using Handler = std::function<net::HttpResponse(const net::HttpRequest&)>;

class Router {
public:
    void get(const std::string& path, Handler h);
    void post(const std::string& path, Handler h);

    // Dispatches to the matching handler, or returns 404/405.
    net::HttpResponse dispatch(const net::HttpRequest& req) const;

private:
    std::unordered_map<std::string, Handler> getRoutes_;
    std::unordered_map<std::string, Handler> postRoutes_;
};

} // namespace server
