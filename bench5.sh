#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DSOLUTION5_NOFORK="${NOFORK:-OFF}"
cmake --build build --target solution5 -j
exec python3 tools/benchmark5.py "$@"
