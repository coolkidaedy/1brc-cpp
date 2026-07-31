// 1BRC — my C++ solution.
//
// Write process_input() below. Everything else is done.
//
// Run all commands from the repo root: /Users/aedin/1brc/1brc/1brc-cpp
//
//   1. Build      ninja -C build solution
//   2. Test       ./run_tests.sh                    should print "12 passed"
//   3. Time       time ./build/solution measurements.txt
//
// measurements.txt is a symlink to the 100M-row file (1.4 GB).
// Step 3 only means something after step 2 passes. Take the best of 3 runs;
// the first run is slower because the file isn't in the page cache yet.
//
// To beat: ./build/01_baseline takes 8.6s on the same file.
// Don't diff against 01_baseline though — its rounding is wrong. run_tests.sh
// checks against the official expected output, which is what counts.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include <thread>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

constexpr int NUM_THREADS = 8;
// Deliberately >> NUM_THREADS so a thread stuck on a slow core just claims
// fewer chunks over the same wall-clock time, instead of blocking everyone
// else at join() like a fixed even split would.
constexpr int NUM_CHUNKS = 200;

struct Record {
    uint64_t cnt;
    int64_t sum;
    int64_t min;
    int64_t max;
};

// Open-addressing hash table (linear probing) over one contiguous
// std::vector<Slot>, instead of std::unordered_map's separately
// heap-allocated bucket nodes. Fixed, non-resizing capacity — safe because
// the official 1BRC spec guarantees at most 10,000 unique station names, so
// even worst case this stays well under 20% load factor.
class FlatMap {
public:
    FlatMap() : slots_(CAPACITY) {}

    // Inserts a new station (first occurrence) or folds value into its
    // existing running stats.
    void update(const std::string &key, int64_t value) {
        size_t idx = index_of(key);
        while (!slots_[idx].key.empty()) {
            if (slots_[idx].key == key) {
                Record &r = slots_[idx].record;
                r.min = std::min(r.min, value);
                r.max = std::max(r.max, value);
                r.sum += value;
                ++r.cnt;
                return;
            }
            idx = (idx + 1) & MASK;
        }
        slots_[idx].key = key;
        slots_[idx].record = Record{1, value, value, value};
    }

    // Folds every entry of `other` into this table, combining records for
    // any station present in both.
    void merge(const FlatMap &other) {
        for (const auto &slot : other.slots_) {
            if (slot.key.empty()) continue;
            merge_one(slot.key, slot.record);
        }
    }

    const Record *find(const std::string &key) const {
        size_t idx = index_of(key);
        while (!slots_[idx].key.empty()) {
            if (slots_[idx].key == key) return &slots_[idx].record;
            idx = (idx + 1) & MASK;
        }
        return nullptr;
    }

    template <typename F>
    void for_each(F f) const {
        for (const auto &slot : slots_) {
            if (!slot.key.empty()) f(slot.key, slot.record);
        }
    }

private:
    static constexpr size_t CAPACITY = 1 << 16; // must be a power of two
    static constexpr size_t MASK = CAPACITY - 1;

    struct Slot {
        std::string key; // empty key == empty slot; station names are never empty
        Record record;
    };

    static size_t index_of(const std::string &key) {
        return std::hash<std::string>{}(key) & MASK;
    }

    void merge_one(const std::string &key, const Record &rec) {
        size_t idx = index_of(key);
        while (!slots_[idx].key.empty()) {
            if (slots_[idx].key == key) {
                Record &r = slots_[idx].record;
                r.cnt += rec.cnt;
                r.sum += rec.sum;
                r.min = std::min(r.min, rec.min);
                r.max = std::max(r.max, rec.max);
                return;
            }
            idx = (idx + 1) & MASK;
        }
        slots_[idx].key = key;
        slots_[idx].record = rec;
    }

    std::vector<Slot> slots_;
};

using DB = FlatMap;

// Every value has exactly one decimal digit, e.g. "-12.3" or "5.0", so it can
// be represented exactly as an integer count of tenths ("-12.3" -> -123).
// Integer sums are exact and order-independent, unlike float/double sums.
static int64_t parse_tenths(const char *data, size_t start) {
    bool neg = false;
    size_t i = start;
    if (data[i] == '-') {
        neg = true;
        ++i;
    }
    int64_t v = 0;
    for (; data[i] != '.'; ++i) {
        v = v * 10 + (data[i] - '0');
    }
    ++i; // skip '.'
    v = v * 10 + (data[i] - '0');
    return neg ? -v : v;
}

// Map phase: parses [start, end) of the mmap'd buffer (must land on line
// boundaries) into its own private map. No locking needed — each thread only
// ever touches its own db.
static void process_range(const char *data, size_t start, size_t end, DB &db) {
    size_t pos = start;
    while (pos < end) {
        size_t semi = pos;
        while (data[semi] != ';') ++semi;
        std::string station(data + pos, semi - pos);

        size_t nl = semi + 1;
        while (data[nl] != '\n') ++nl;

        int64_t fp_value = parse_tenths(data, semi + 1);
        pos = nl + 1;

        db.update(station, fp_value);
    }
}

// Work-stealing worker: repeatedly claims the next unclaimed chunk index and
// processes it, until no chunks remain.
static void worker(const char *data, const std::array<size_t, NUM_CHUNKS + 1> &boundaries,
                    std::atomic<size_t> &next_chunk, DB &db) {
    size_t i;
    while ((i = next_chunk.fetch_add(1, std::memory_order_relaxed)) < NUM_CHUNKS) {
        process_range(data, boundaries[i], boundaries[i + 1], db);
    }
}

DB process_input(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Failed to open " << path << "\n";
        std::exit(1);
    }

    struct stat st{};
    fstat(fd, &st);
    size_t size = static_cast<size_t>(st.st_size);

    char *data = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    if (data == MAP_FAILED) {
        std::cerr << "Failed to mmap " << path << "\n";
        std::exit(1);
    }

    // Split into NUM_CHUNKS line-boundary-snapped ranges, well more than
    // NUM_THREADS, so threads pull chunks dynamically instead of each owning
    // a fixed share up front.
    std::array<size_t, NUM_CHUNKS + 1> boundaries;
    boundaries[0] = 0;
    boundaries[NUM_CHUNKS] = size;
    for (int i = 1; i < NUM_CHUNKS; ++i) {
        size_t p = size * i / NUM_CHUNKS;
        while (p < size && data[p] != '\n') ++p;
        if (p < size) ++p;
        boundaries[i] = p;
    }

    std::atomic<size_t> next_chunk{0};
    std::array<DB, NUM_THREADS> thread_dbs;
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, data, std::cref(boundaries), std::ref(next_chunk), std::ref(thread_dbs[i]));
    }
    for (auto &t : threads) t.join();

    munmap(data, size);

    // Reduce phase: fold all per-thread maps into one.
    DB db = std::move(thread_dbs[0]);
    for (int i = 1; i < NUM_THREADS; ++i) {
        db.merge(thread_dbs[i]);
    }

    return db;
}


// Integer division that rounds toward -infinity (C++'s / truncates toward
// zero instead). Needed so half-up-toward-+inf rounding below is correct for
// negative sums too, not just positive ones.
static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

// Writes an exact tenths-count (e.g. -123 for -12.3) as "-12.3".
static void write_tenths(std::ostream &out, int64_t tenths) {
    if (tenths < 0) {
        out << '-';
        tenths = -tenths;
    }
    out << (tenths / 10) << '.' << (tenths % 10);
}

void format_output(std::ostream &out, const DB &db) {
    std::vector<std::string> names;
    db.for_each([&](const std::string &k, const Record &) { names.push_back(k); });
    // Sorting UTF-8 lexicographically by byte == sorting by codepoint.
    std::ranges::sort(names);

    std::string delim = "";
    out << "{";
    for (const auto &k : names) {
        const Record &record = *db.find(k);
        int64_t cnt = static_cast<int64_t>(record.cnt);

        // record.sum is already an exact tenths count, so rounding it first
        // (like the float version had to) is unnecessary — only the mean
        // itself (sum/cnt) needs half-up-toward-+inf rounding to one decimal.
        int64_t mean_tenths = floor_div(2 * record.sum + cnt, 2 * cnt);

        out << std::exchange(delim, ", ") << k << "=";
        write_tenths(out, record.min);
        out << "/";
        write_tenths(out, mean_tenths);
        out << "/";
        write_tenths(out, record.max);
    }
    out << "}\n";
}

int main(int argc, char **argv) {
    const std::string path = (argc > 1) ? argv[1] : "measurements.txt";

    auto db = process_input(path);
    format_output(std::cout, db);
}
