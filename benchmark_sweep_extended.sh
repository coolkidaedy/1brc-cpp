#!/bin/bash
# Full, self-contained re-sweep: threads 1-8 x chunks (20-2000), all under one
# consistent methodology (randomized order + warm-up run), written to a fresh
# file rather than appended to benchmark_results.csv. That original file was
# collected sequentially with no warm-up, so mixing it with this run would
# make it impossible to tell whether differences come from the parameters or
# from methodology/system-state drift between the two sweeps.
# Combination order is shuffled up front (not run sequentially) so thermal
# drift / background noise over this long run doesn't systematically bias
# any particular thread or chunk count. Each combination gets one discarded
# warm-up run before its 10 measured runs, so a cold page cache never
# pollutes the measured times.
set -e

cd "$(dirname "$0")"

SRC=src/solution3.cpp
RESULTS=benchmark_results_v2.csv
RUNS_PER_COMBO=10

LOW_CHUNKS="20 40 60 80 100 120 140 160 180 200"
HIGH_CHUNKS="300 400 500 600 700 800 900 1000 1200 1400 1600 1800 2000"
ALL_CHUNKS="$LOW_CHUNKS $HIGH_CHUNKS"
THREAD_VALUES="1 2 3 4 5 6 7 8"

cp "$SRC" "${SRC}.bak"
restore() {
    if [ -f "${SRC}.bak" ]; then
        mv "${SRC}.bak" "$SRC"
        ninja -C build solution3 > /dev/null 2>&1 || true
    fi
}
trap restore EXIT

combos_file=$(mktemp)
for t in $THREAD_VALUES; do
    for c in $ALL_CHUNKS; do
        echo "$t $c" >> "$combos_file"
    done
done

# macOS has no `shuf`; tag each line with a random sort key instead.
shuffled_file=$(mktemp)
awk 'BEGIN{srand()} {print rand() "\t" $0}' "$combos_file" | LC_ALL=C sort -n | cut -f2- > "$shuffled_file"
rm -f "$combos_file"

total_combos=$(wc -l < "$shuffled_file" | tr -d ' ')
echo "Total combos to test: $total_combos ($((total_combos * RUNS_PER_COMBO)) measured runs + $total_combos warm-ups)"

echo "threads,chunks,run,seconds" > "$RESULTS"

TIMEFORMAT='%R'
combo_num=0

while read -r threads chunks; do
    combo_num=$((combo_num + 1))

    sed -i '' "s/constexpr int NUM_THREADS = .*/constexpr int NUM_THREADS = ${threads};/" "$SRC"
    sed -i '' "s/constexpr int NUM_CHUNKS = .*/constexpr int NUM_CHUNKS = ${chunks};/" "$SRC"
    ninja -C build solution3 > /dev/null

    # Discarded warm-up run: ensures the page cache is warm before any of
    # the 10 measured runs below, so none of them absorb a cold-cache penalty.
    ./build/solution3 measurements.txt >/dev/null 2>/dev/null

    for run in $(seq 1 "$RUNS_PER_COMBO"); do
        elapsed=$( { time ./build/solution3 measurements.txt >/dev/null 2>/dev/null; } 2>&1 )
        echo "${threads},${chunks},${run},${elapsed}" >> "$RESULTS"
    done
    echo "[$combo_num/$total_combos] done: threads=${threads} chunks=${chunks}"
done < "$shuffled_file"

rm -f "$shuffled_file"
echo "Sweep complete. $((total_combos * RUNS_PER_COMBO)) runs recorded in $RESULTS"
