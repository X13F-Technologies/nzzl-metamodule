#!/bin/bash
# Compile and run the native test harnesses (no Rack/MetaModule needed).
# Every tests/test_*.cc is a standalone binary with its own main().
set -e
cd "$(dirname "$0")"

status=0
for src in test_*.cc; do
    name="${src%.cc}"
    printf '\n── %s ──────────────────────────────────────\n' "$name"
    c++ -std=c++20 -Wall -Wextra -O1 -o "/tmp/nzzl_$name" "$src"
    "/tmp/nzzl_$name" || status=1
done

printf '\n'
if [ "$status" -ne 0 ]; then
    echo "SOME SUITES FAILED"
else
    echo "all suites passed"
fi
exit "$status"
