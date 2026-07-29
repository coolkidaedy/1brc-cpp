#!/bin/bash
# Runs ./build/solution against the 12 official sample cases.
set -e

SAMPLES=/Users/aedin/1brc/1brc/src/test/resources/samples
SOLUTION=$(dirname "$0")/build/solution

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
