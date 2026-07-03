#!/bin/bash
# Compile and run the native test harness (no Rack/MetaModule needed)
set -e
cd "$(dirname "$0")"
c++ -std=c++20 -Wall -Wextra -O1 -o /tmp/nzzl_tests test_pattern.cc
/tmp/nzzl_tests
