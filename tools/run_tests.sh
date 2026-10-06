#!/usr/bin/env bash
# Builds and runs the worldserver-independent unit tests in tests/ (pure decision logic only).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build-tests"
mkdir -p "$out"

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$root/src/DungeonLead" \
    "$root"/tests/*.cpp -o "$out/run_tests"
"$out/run_tests"

# replay tools (tools/run_replay) - Python, also worldserver-independent
(cd "$root" && python3 -m unittest tests.test_run_replay)
