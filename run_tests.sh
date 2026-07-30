#!/bin/bash
# Runs a build/ binary (default: solution) against the 12 official sample cases.
# Usage: ./run_tests.sh [binary_name]
set -e

SAMPLES=/Users/aedin/1brc/1brc/src/test/resources/samples
SOLUTION=$(dirname "$0")/build/${1:-solution}

pass=0
fail=0
for input in "$SAMPLES"/*.txt; do
    expected="${input%.txt}.out"
    if diff <("$SOLUTION" "$input") "$expected" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL  $(basename "$input")"
    fi
done

echo "$pass passed, $fail failed"
