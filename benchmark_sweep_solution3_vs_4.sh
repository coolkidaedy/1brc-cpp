#!/bin/bash
# Compares solution3 (std::unordered_map) against solution4 (flat
# open-addressed table) across a range of NUM_CHUNKS values, with
# NUM_THREADS left untouched (fixed at whatever's currently set in each
# file). Combination order is shuffled up front so thermal drift / system
# noise over the run doesn't systematically favor one solution over the
# other just because of when it happened to run. Each combination gets one
# discarded warm-up run before its 10 measured runs.
set -e

cd "$(dirname "$0")"

RESULTS=benchmark_results_solution3_vs_4.csv
RUNS_PER_COMBO=10
CHUNK_VALUES="20 40 60 80 100 120 140 160 180 200 300 400 500 600 700 800 900 1000"
SOLUTIONS="solution3 solution4"

for sol in $SOLUTIONS; do
    cp "src/${sol}.cpp" "src/${sol}.cpp.bak"
done
restore() {
    for sol in $SOLUTIONS; do
        if [ -f "src/${sol}.cpp.bak" ]; then
            mv "src/${sol}.cpp.bak" "src/${sol}.cpp"
        fi
    done
    ninja -C build solution3 solution4 > /dev/null 2>&1 || true
}
trap restore EXIT

combos_file=$(mktemp)
for sol in $SOLUTIONS; do
    for c in $CHUNK_VALUES; do
        echo "$sol $c" >> "$combos_file"
    done
done

# macOS has no `shuf`; tag each line with a random sort key instead.
shuffled_file=$(mktemp)
awk 'BEGIN{srand()} {print rand() "\t" $0}' "$combos_file" | LC_ALL=C sort -n | cut -f2- > "$shuffled_file"
rm -f "$combos_file"

total_combos=$(wc -l < "$shuffled_file" | tr -d ' ')
echo "Total combos: $total_combos ($((total_combos * RUNS_PER_COMBO)) measured runs + $total_combos warm-ups)"

echo "solution,chunks,run,seconds" > "$RESULTS"

TIMEFORMAT='%R'
combo_num=0

while read -r sol chunks; do
    combo_num=$((combo_num + 1))
    src="src/${sol}.cpp"

    sed -i '' "s/constexpr int NUM_CHUNKS = .*/constexpr int NUM_CHUNKS = ${chunks};/" "$src"
    ninja -C build "$sol" > /dev/null

    # Discarded warm-up run.
    ./build/"$sol" measurements.txt >/dev/null 2>/dev/null

    for run in $(seq 1 "$RUNS_PER_COMBO"); do
        elapsed=$( { time ./build/"$sol" measurements.txt >/dev/null 2>/dev/null; } 2>&1 )
        echo "${sol},${chunks},${run},${elapsed}" >> "$RESULTS"
    done
    echo "[$combo_num/$total_combos] done: solution=${sol} chunks=${chunks}"
done < "$shuffled_file"

rm -f "$shuffled_file"
echo "Sweep complete. $((total_combos * RUNS_PER_COMBO)) runs recorded in $RESULTS"
