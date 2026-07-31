#!/bin/bash
# Sweeps NUM_THREADS x NUM_CHUNKS for solution3.cpp, timing each combination
# RUNS_PER_COMBO times against the full measurements.txt, and appends every
# run's wall-clock (real) time to benchmark_results.csv.
#
# Restores solution3.cpp to its pre-sweep contents (and rebuilds it) when done,
# whether the sweep finishes normally or is interrupted.
set -e

cd "$(dirname "$0")"

SRC=src/solution3.cpp
RESULTS=benchmark_results.csv
RUNS_PER_COMBO=10
THREAD_VALUES="1 2 3 4"
CHUNK_VALUES="20 40 60 80 100 120 140 160 180 200"

cp "$SRC" "${SRC}.bak"
restore() {
    if [ -f "${SRC}.bak" ]; then
        mv "${SRC}.bak" "$SRC"
        ninja -C build solution3 > /dev/null 2>&1 || true
    fi
}
trap restore EXIT

echo "threads,chunks,run,seconds" > "$RESULTS"

TIMEFORMAT='%R'

total=0
combo=0
for threads in $THREAD_VALUES; do
    for chunks in $CHUNK_VALUES; do
        combo=$((combo + 1))
        sed -i '' "s/constexpr int NUM_THREADS = .*/constexpr int NUM_THREADS = ${threads};/" "$SRC"
        sed -i '' "s/constexpr int NUM_CHUNKS = .*/constexpr int NUM_CHUNKS = ${chunks};/" "$SRC"

        ninja -C build solution3 > /dev/null

        for run in $(seq 1 "$RUNS_PER_COMBO"); do
            elapsed=$( { time ./build/solution3 measurements.txt >/dev/null 2>/dev/null; } 2>&1 )
            echo "${threads},${chunks},${run},${elapsed}" >> "$RESULTS"
            total=$((total + 1))
        done
        echo "[$combo/40] done: threads=${threads} chunks=${chunks} (total runs so far: ${total})"
    done
done

echo "Sweep complete. $total runs recorded in $RESULTS"
