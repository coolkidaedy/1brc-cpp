# The One Billion Row Challenge

C++ implementations of the [One Billion Row Challenge](https://github.com/gunnarmorling/1brc): compute each station's minimum, mean, and maximum temperature, sorted by station name.

## Build and run solution5

Requires a POSIX system (Linux or macOS), CMake 3.27+, and a C++23 compiler.

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target solution5 -j
./build/solution5 measurements.txt 32
```

The optional second argument is the thread count (1..1024). By default it uses hardware concurrency; tiny files use fewer workers to avoid unnecessary table allocations. Release builds use `-O3 -march=native`, so binaries should be built on the target machine. On x86-64, SSE2 scans 16 name bytes at once, SSE4.2 supplies CRC32C hashing, and BMI2 supplies BZHI masking when enabled by the compiler. Other architectures use scalar scanning, portable masking, and a mixed integer hash. They are correctness fallbacks, not performance equivalents to the Xeon path.

## Implementation methodology

[`src/solution5.cpp`](src/solution5.cpp) reimplements the supplied optimization blueprint:

1. **Guarded memory mapping.** Reserve a page-rounded anonymous mapping plus one guard page, then map the file over its front. The readable zero tail permits fixed-width loads at EOF. Keys point into the mapping, which remains alive until process exit.
2. **Dynamic scheduling.** Workers claim line-aligned chunks from a relaxed atomic counter, with 24 chunks per worker. Each worker owns a 32,768-slot open-addressing table, avoiding shared updates.
3. **Three independent cursors.** Split each chunk into three line-aligned lanes and advance explicit scalar cursors in lockstep. The 107-byte maximum record size bounds the number of steps between range checks; separate loops drain the remaining records.
4. **Compact keys.** Each 48-byte slot stores two inline key words, a pointer and length, sum/count, and minimum/maximum. Short keys are zero-padded; keys longer than 16 bytes use a 0xff sentinel plus a length/tail comparison. Two hardware CRC32C operations hash the key image on supported x86 builds. Linear probing resolves collisions.
5. **Branchless temperature conversion.** An eight-byte load locates the decimal point with a bit mask. Masking, shifting, and multiplication extract integer tenths; an arithmetic sign mask handles negative values. Aggregation is exact and independent of worker order.
6. **Exact rounding.** Means use `floor((2 * sum + count) / (2 * count))` in integer tenths. Explicit correction of C++ truncating division implements ties toward positive infinity, including negative means. Output is formatted into one string after merging and sorting.
7. **Deferred teardown.** Fork before mapping. The child calculates and pipes the complete result to the parent, then closes the pipe before `_exit`. The parent validates completion, writes stdout, and exits without waiting for successful child teardown. Missing files, failed workers, and I/O errors return nonzero.

The input contract is newline-terminated `Name;-?d[d].d`, with values -99.9 through 99.9, 1..100-byte valid UTF-8 names **without NUL**, and at most 10,000 unique stations and one billion records. UTF-8 alone does not exclude NUL; that is an additional requirement of the key-image representation. The parser assumes valid challenge records; it is not a general CSV parser or malformed-input validator. Empty files produce `{}`. Missing final newlines are rejected. Names use UTF-8 byte order, as in solution4 and the official samples.

For profiling, sanitizers, PGO, or timing that includes mapping teardown, build without the fork:

```sh
cmake -S src -B build-nofork -DCMAKE_BUILD_TYPE=Release -DSOLUTION5_NOFORK=ON
cmake --build build-nofork --target solution5 -j
```

The default timing measures time until the result-producing parent exits, **not** time until all worker resources have been reclaimed. The child may briefly outlive the parent. Parent-only CPU accounting does not describe worker CPU usage. Use the no-fork build for end-to-end resource measurements.

## Correctness verification

Fetch the upstream sample suite (or set `SAMPLES` to an existing checkout):

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/gunnarmorling/1brc.git 1brc
git -C 1brc sparse-checkout set src/test/resources/samples
SAMPLES=1brc/src/test/resources/samples ./run_tests.sh solution5
python3 tools/test_solution5.py build/solution5
```

`run_tests.sh` checks actual exit status and exact output, fails when no samples exist, and returns nonzero on any failure. The independent differential tests cover every temperature in the supported range, positive/negative rounding ties, names of lengths 1..100, shared long prefixes, UTF-8, 10,000 distinct stations, randomized records, exact/partial page boundaries, empty files, multiple thread counts, and error paths.

Local validation used macOS ARM64 and AppleClang 21.0.0, with upstream samples at commit `db064194be375edc02d6dbcd21268ad40f7e2869`:

- Native release: all 12 official samples; 42 differential and 4 error-path checks.
- Native no-fork AddressSanitizer/UndefinedBehaviorSanitizer: the same checks.
- Cross-built x86-64 (`-march=haswell`), executed through macOS translation: the same checks, including SSE2, CRC32C, and BZHI code paths.

This validates correctness locally; it does not establish native Linux/GCC performance. The billion-row dataset and original Xeon host were not available for this implementation session.

## Supplied benchmark results

The unmodified attached CSV is preserved as [`benchmark_results_all_solutions.csv`](benchmark_results_all_solutions.csv). The table below summarizes **all 215 supplied observations**, without removing outliers. Median speedup is the other program's median divided by solution5's median. Run counts are unequal and should be considered when comparing variability.

| Program | Runs | Min (s) | Median (s) | Max (s) | Median / solution5 |
|---|---:|---:|---:|---:|---:|
| solution1 | 5 | 319.16 | 327.960 | 341.53 | 762.70x |
| 01_baseline | 10 | 124.94 | 125.615 | 126.25 | 292.13x |
| solution2 | 50 | 54.17 | 54.605 | 55.09 | 126.99x |
| solution3 | 50 | 5.77 | 5.830 | 5.97 | 13.56x |
| solution4 | 50 | 2.56 | 2.710 | 2.97 | 6.30x |
| solution5 | 50 | 0.41 | 0.430 | 0.53 | 1.00x |

Reproduce this summary with `node tools/summarize_benchmarks.mjs`. The CSV label `solution1` is retained verbatim; the current CMake target is named `solution`, and the CSV does not establish its source revision or mapping to that target.

The accompanying write-up reports one billion rows (about 13.8 GB), warm page cache, an AWS Intel Xeon Platinum 8488C (Sapphire Rapids), 16 cores/32 hardware threads, about 3.2 GHz all-core, GCC 13.3, and Linux 7.0. It describes solution5 using 32 threads and three lanes. **Those environment details are reported by the supplied write-up, not independently recorded in the CSV.** The CSV contains only solution, run, sequence, start epoch, and elapsed seconds; it does not identify source commits, compiler flags, thread counts, input checksums, or warmup procedure.

These are historical supplied measurements, **not measurements of the new source added here**. At 13.8 GB, the CSV's 0.430-second median corresponds to approximately 32.1 GB/s of input processed. The write-up's separate 0.40-second best, 129.6-second baseline, and 2.07-second retuned solution4 results do not appear in this CSV and are not mixed into this table. In particular, the CSV supports 6.30x versus its solution4 runs, not the write-up's approximately 4.8x comparison against a different tuned configuration.

## Reproducible benchmark protocol

1. Record the source commit, compiler/version/flags, CPU/core/thread counts, OS, file size, row count, checksum, and explicit thread count. Record whether fork or no-fork is used.
2. Generate the input using the [official generator](https://github.com/gunnarmorling/1brc#running-the-challenge). Use a full upstream checkout for generation; the sparse sample checkout above does not contain its Java sources. Follow its build instructions, run `./create_measurements.sh 1000000000`, and create a golden output with its known-correct Java baseline. The C++ `01_baseline` is retained for performance comparison, not used as a rounding oracle.
3. Run the official and differential tests before timing. Compare the full large-file output against the golden output.
4. Run one full warmup and discard its timing. Measure at least five full process wall times with the input resident in page cache. Report minimum and median; preserve every raw observation. Ensure enough memory for the dataset and tables.
5. Compare alternatives in interleaved/back-to-back batches on the same host, with equal configurations. Allow deferred child teardown to settle between trials. Repeat batches when shared-host contention shifts results. Cold-cache I/O is a separate benchmark.

The harness builds solution5, performs the discarded warmup, validates every output against the golden file, uses a monotonic wall clock around the complete process, and writes raw CSV:

```sh
bash bench5.sh measurements.txt golden_output.txt --threads 32 --runs 5 --csv benchmark_solution5_local.csv
NOFORK=ON bash bench5.sh measurements.txt golden_output.txt --threads 32 --runs 5 --csv benchmark_solution5_nofork.csv
```

The harness pauses one second between trials to reduce interference from deferred cleanup; on hosts where teardown takes longer, use no-fork for resource-inclusive comparisons or increase the pause. It does not claim to recreate the original CSV's undocumented execution procedure.

The supplied blueprint also reports negative results for eager page population, single-CRC hashing, extra lanes, and PGO. Those are useful hypotheses for future A/B experiments, not independently reproduced findings for this reimplementation.
