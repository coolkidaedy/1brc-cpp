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
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <thread>
#include <chrono>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

constexpr int NUM_THREADS = 2;

struct Record {
    uint64_t cnt;
    float sum;
    float min;
    float max;
};

using DB = std::unordered_map<std::string, Record>;

// Java rounds half-up toward +inf. Not std::round, not printf: -1.25 -> -1.2, not -1.3.
static float round1(float v) { return std::floor(v * 10.0 + 0.5) / 10.0; }


// Parses [start, end) of the mmap'd buffer (must land on line boundaries)
// and merges results into the shared db, guarded by db_mutex.
static void process_range(const char *data, size_t start, size_t end, DB &db, std::mutex &db_mutex) {
    size_t pos = start;
    while (pos < end) {
        size_t semi = pos;
        while (data[semi] != ';') ++semi;
        std::string station(data + pos, semi - pos);

        size_t nl = semi + 1;
        while (data[nl] != '\n') ++nl;
        std::string value(data + semi + 1, nl - semi - 1);

        float fp_value = std::stof(value);
        pos = nl + 1;

        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = db.find(station);
        if (it == db.end()) {
            db.emplace(station, Record{1, fp_value, fp_value, fp_value});
            continue;
        }
        it->second.min = std::min(it->second.min, static_cast<double>(fp_value));
        it->second.max = std::max(it->second.max, static_cast<double>(fp_value));
        it->second.sum += fp_value;
        ++it->second.cnt;
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

    // Snap NUM_THREADS-1 split points to the next line boundary so no
    // thread's range straddles a partial record.
    std::vector<size_t> boundaries(NUM_THREADS + 1);
    boundaries[0] = 0;
    boundaries[NUM_THREADS] = size;
    for (int i = 1; i < NUM_THREADS; ++i) {
        size_t p = size * i / NUM_THREADS;
        while (p < size && data[p] != '\n') ++p;
        if (p < size) ++p;
        boundaries[i] = p;
    }

    DB db;
    std::mutex db_mutex;
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(process_range, data, boundaries[i], boundaries[i + 1],
                              std::ref(db), std::ref(db_mutex));
    }
    for (auto &t : threads) t.join();

    munmap(data, size);

    return db;
}


void format_output(std::ostream &out, const DB &db) {
    std::vector<std::string> names(db.size());
    // Sorting UTF-8 lexicographically by byte == sorting by codepoint.
    std::ranges::copy(db | std::views::keys, names.begin());
    std::ranges::sort(names);

    std::string delim = "";
    out << std::setiosflags(out.fixed | out.showpoint) << std::setprecision(1);
    out << "{";
    for (const auto &k : names) {
        const auto &record = db.find(k)->second;

        // Java rounds the sum before dividing, then rounds again. Keep both.
        double mean = round1(round1(record.sum) / static_cast<double>(record.cnt));

        out << std::exchange(delim, ", ") << k << "=" << round1(record.min) << "/"
            << mean << "/" << round1(record.max);
    }
    out << "}\n";
}

int main(int argc, char **argv) {
    const std::string path = (argc > 1) ? argv[1] : "measurements.txt";

    auto db = process_input(path);
    format_output(std::cout, db);
}
