#include "router.h"

namespace server {

void Router::get(const std::string& path, Handler h) { getRoutes_[path] = std::move(h); }
void Router::post(const std::string& path, Handler h) { postRoutes_[path] = std::move(h); }

net::HttpResponse Router::dispatch(const net::HttpRequest& req) const {
    const std::unordered_map<std::string, Handler>* table = nullptr;
    if (req.method == "GET") {
        table = &getRoutes_;
    } else if (req.method == "POST") {
        table = &postRoutes_;
    } else {
        return net::makeJsonResponse(405, "Method Not Allowed", R"({"error":"method not allowed"})");
    }

    auto it = table->find(req.target);
    if (it == table->end()) {
        return net::makeJsonResponse(404, "Not Found", R"({"error":"not found"})");
    }
    return it->second(req);
}

} // namespace server
