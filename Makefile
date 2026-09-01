CXX      := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread -Iinclude
LDFLAGS  := -pthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  REACTOR_SRC := src/net/epoll_reactor.cpp
else
  REACTOR_SRC := src/net/kqueue_reactor.cpp
endif

NET_SRCS    := src/net/socket.cpp src/net/event_loop.cpp src/net/thread_pool.cpp \
               src/net/http_parser.cpp src/net/connection.cpp $(REACTOR_SRC)
SERVER_SRCS := src/server/main.cpp src/server/http_server.cpp src/server/router.cpp
LB_SRCS     := src/loadbalancer/main.cpp src/loadbalancer/load_balancer.cpp \
               src/loadbalancer/upstream_pool.cpp src/loadbalancer/health_checker.cpp

NET_OBJS    := $(patsubst src/%.cpp,build/obj/%.o,$(NET_SRCS))
SERVER_OBJS := $(patsubst src/%.cpp,build/obj/%.o,$(SERVER_SRCS))
LB_OBJS     := $(patsubst src/%.cpp,build/obj/%.o,$(LB_SRCS))

TEST_BINS := build/tests/test_http_parser build/tests/test_thread_pool build/tests/test_upstream_pool

.PHONY: all clean test demo debug
all: build/server build/loadbalancer

build/server: $(NET_OBJS) $(SERVER_OBJS)
	@mkdir -p build
	$(CXX) $(LDFLAGS) -o $@ $^

build/loadbalancer: $(NET_OBJS) $(LB_OBJS)
	@mkdir -p build
	$(CXX) $(LDFLAGS) -o $@ $^

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Isrc/server -c -o $@ $<

build/tests/test_http_parser: tests/test_http_parser.cpp $(NET_OBJS)
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

build/tests/test_thread_pool: tests/test_thread_pool.cpp $(NET_OBJS)
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

build/tests/test_upstream_pool: tests/test_upstream_pool.cpp $(NET_OBJS) build/obj/loadbalancer/upstream_pool.o
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

test: $(TEST_BINS)
	@chmod +x tests/run_tests.sh
	./tests/run_tests.sh

debug: CXXFLAGS += -fsanitize=thread -g -O0
debug: LDFLAGS += -fsanitize=thread
debug: clean all

demo: all
	@chmod +x run-demo.sh
	./run-demo.sh

clean:
	rm -rf build
