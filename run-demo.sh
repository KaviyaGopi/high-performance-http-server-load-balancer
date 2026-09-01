#!/usr/bin/env bash
set -euo pipefail

PORTS=(9001 9002 9003)
LB_PORT=8080
PIDS=()

CLEANED_UP=0
cleanup() {
    if [ "$CLEANED_UP" -eq 1 ]; then return; fi
    CLEANED_UP=1
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT INT TERM

for p in "${PORTS[@]}"; do
    ./build/server --port "$p" &
    PIDS+=($!)
done

sleep 1 # let servers bind before the load balancer's health checks start

UPSTREAMS=""
for p in "${PORTS[@]}"; do
    UPSTREAMS+="127.0.0.1:$p,"
done
UPSTREAMS="${UPSTREAMS%,}"

./build/loadbalancer --port "$LB_PORT" --upstreams "$UPSTREAMS" &
PIDS+=($!)

sleep 0.5

echo ""
echo "Load balancer running on http://127.0.0.1:$LB_PORT"
echo "Upstream servers on ports: ${PORTS[*]}"
echo ""
echo "Try in another terminal:"
echo "  for i in \$(seq 1 9); do curl -s http://127.0.0.1:$LB_PORT/; echo; done"
echo "  curl -s http://127.0.0.1:$LB_PORT/health"
echo "  ab -n 500 -c 50 http://127.0.0.1:$LB_PORT/"
echo ""
echo "Press Ctrl+C to stop all processes."
echo ""

wait
