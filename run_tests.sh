#!/bin/bash
# Runs a build/ binary against the official sample cases.
# Usage: ./run_tests.sh [binary_name]
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
SAMPLES=${SAMPLES:-"$ROOT/1brc/src/test/resources/samples"}
SOLUTION=${1:-solution5}
[[ "$SOLUTION" == */* ]] || SOLUTION="$ROOT/build/$SOLUTION"
shopt -s nullglob
inputs=("$SAMPLES"/*.txt)
(( ${#inputs[@]} )) || { echo "No samples found: $SAMPLES" >&2; exit 1; }
actual=$(mktemp)
trap 'rm -f "$actual"' EXIT

pass=0
fail=0
for input in "${inputs[@]}"; do
    expected="${input%.txt}.out"
    if "$SOLUTION" "$input" >"$actual" && diff -u "$expected" "$actual"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL  $(basename "$input")"
    fi
done

echo "$pass passed, $fail failed"
(( fail == 0 ))
