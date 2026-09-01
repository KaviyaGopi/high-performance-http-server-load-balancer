# High-Performance HTTP Server & Load Balancer

A multithreaded HTTP/1.1 server and a Layer-7 round-robin reverse-proxy load
balancer, both built from scratch in C++17 on top of raw BSD sockets — no
external HTTP or networking libraries.

## Architecture

Both binaries share the same concurrency model: **one reactor thread + a
fixed-size worker thread pool**, created once at startup and never touched
again for the life of the process. No thread is ever spawned per connection or
per request.

- The **reactor thread** owns the event loop (epoll on Linux, kqueue on
  macOS/BSD — an abstraction in [`net::EventLoop`](include/net/event_loop.h)
  picks the right backend at compile time) and the listening socket. It is
  the only thread that ever calls `accept()` or touches the event loop.
- On a readable/writable fd, the reactor **disables further readiness
  delivery for that fd** and hands the work to the **fixed worker pool**
  ([`net::ThreadPool`](include/net/thread_pool.h)) as a queued task. This is
  what prevents "thread thrashing": a burst of traffic grows the task queue,
  not the number of threads.
- A worker reads, parses ([`net::HttpParser`](include/net/http_parser.h),
  an incremental HTTP/1.1 state machine), handles the request, writes the
  response, then posts a small "re-arm" request back to the reactor rather
  than touching the event loop itself — keeping a strict single-writer rule
  on the reactor's state.

The **load balancer** ([`lb::LoadBalancer`](include/loadbalancer/load_balancer.h))
reuses the exact same reactor/pool core. Per request, a worker picks the next
upstream via lock-free round-robin
([`lb::UpstreamPool`](include/loadbalancer/upstream_pool.h)), opens a
short-lived connection to it, forwards the request, and relays the response
back. A dedicated background thread
([`lb::HealthChecker`](include/loadbalancer/health_checker.h)) polls each
upstream's `/health` endpoint and skips unhealthy ones in the rotation.

## Build & run

Requires only a C++17 compiler (g++ or clang++) and `make` — no CMake, no
external dependencies.

```sh
make            # builds build/server and build/loadbalancer
make test       # builds and runs the unit tests
make demo       # launches 3 server instances (9001-9003) + a load balancer (8080)
```

With the demo running:

```sh
for i in $(seq 1 9); do curl -s http://127.0.0.1:8080/; echo; done   # watch it round-robin
curl -s http://127.0.0.1:8080/health
ab -n 500 -c 50 http://127.0.0.1:8080/                               # load test
```

`make debug` builds with `-fsanitize=thread`, useful given the hand-rolled
concurrency.

## Scope and known limitations

This is a portfolio-scale MVP; some things are deliberately out of scope
rather than overlooked:

- **HTTP/1.1 parsing** supports request line, headers, and
  `Content-Length` bodies with keep-alive. Chunked transfer-encoding,
  trailers, and pipelining are not implemented.
- **Upstream connections are not pooled** — the load balancer opens a new
  TCP connection to the chosen upstream per proxied request.
- **No TLS, no HTTP/2, no config file** — the load balancer's upstream list
  is set via CLI flags.
- **"Zero dropped connections during load spikes"** is backed by
  non-blocking accept + OS listen backlog absorbing bursts, the fixed
  worker pool queuing bursts instead of spawning threads, and
  skip-unhealthy-upstream routing — not by explicit backpressure/queue
  bounding, retries, or connection draining on shutdown. Under sustained
  (not just bursty) overload the task queue grows unbounded; a production
  version would bound it and shed load.
