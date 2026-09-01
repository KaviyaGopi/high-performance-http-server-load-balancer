#!/usr/bin/env bash
set -euo pipefail

status=0
for t in build/tests/test_http_parser build/tests/test_thread_pool build/tests/test_upstream_pool; do
    echo "--- running $t ---"
    if ! "./$t"; then
        status=1
    fi
done

exit $status
