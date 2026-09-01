#include "net/http_parser.h"

#include <cassert>
#include <cstdio>
#include <string>

using net::HttpParser;
using net::ParseStatus;

namespace {

int failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    }
}

void testSimpleGet() {
    HttpParser p;
    std::string req = "GET /health HTTP/1.1\r\nHost: x\r\n\r\n";
    auto status = p.consume(req.data(), req.size());
    check(status == ParseStatus::Complete, "simple GET should complete");
    check(p.request().method == "GET", "method should be GET");
    check(p.request().target == "/health", "target should be /health");
    check(p.request().keepAlive == true, "HTTP/1.1 defaults to keep-alive");
}

void testPostWithBody() {
    HttpParser p;
    std::string req = "POST /api/echo HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
    auto status = p.consume(req.data(), req.size());
    check(status == ParseStatus::Complete, "POST with body should complete");
    check(p.request().body == "hello", "body should be 'hello'");
}

void testSplitAcrossReads() {
    HttpParser p;
    std::string part1 = "GET / HTTP/1.1\r\nContent-Le";
    std::string part2 = "ngth: 3\r\n\r\nabc";
    auto s1 = p.consume(part1.data(), part1.size());
    check(s1 == ParseStatus::NeedMoreData, "partial request needs more data");
    auto s2 = p.consume(part2.data(), part2.size());
    check(s2 == ParseStatus::Complete, "request should complete once fed the rest");
    check(p.request().body == "abc", "body should be 'abc'");
}

void testConnectionClose() {
    HttpParser p;
    std::string req = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
    p.consume(req.data(), req.size());
    check(p.request().keepAlive == false, "explicit Connection: close should disable keep-alive");
}

void testHttp10DefaultsClose() {
    HttpParser p;
    std::string req = "GET / HTTP/1.0\r\n\r\n";
    p.consume(req.data(), req.size());
    check(p.request().keepAlive == false, "HTTP/1.0 without keep-alive header defaults to close");
}

void testMalformedRequestLine() {
    HttpParser p;
    std::string req = "GARBAGE\r\n\r\n";
    auto status = p.consume(req.data(), req.size());
    check(status == ParseStatus::Error, "malformed request line should error");
}

void testResetForKeepAlive() {
    HttpParser p;
    std::string req1 = "GET /a HTTP/1.1\r\n\r\n";
    p.consume(req1.data(), req1.size());
    check(p.request().target == "/a", "first request target should be /a");
    p.reset();
    std::string req2 = "GET /b HTTP/1.1\r\n\r\n";
    auto status = p.consume(req2.data(), req2.size());
    check(status == ParseStatus::Complete, "second request should parse after reset");
    check(p.request().target == "/b", "second request target should be /b");
}

} // namespace

int main() {
    testSimpleGet();
    testPostWithBody();
    testSplitAcrossReads();
    testConnectionClose();
    testHttp10DefaultsClose();
    testMalformedRequestLine();
    testResetForKeepAlive();

    if (failures == 0) {
        std::printf("test_http_parser: all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_http_parser: %d failure(s)\n", failures);
    return 1;
}
