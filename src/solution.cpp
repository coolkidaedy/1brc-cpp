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
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <thread>
#include <chrono>

struct Record {
    uint64_t cnt;
    double sum;
    double min;
    double max;
};

using DB = std::unordered_map<std::string, Record>;

// Java rounds half-up toward +inf. Not std::round, not printf: -1.25 -> -1.2, not -1.3.
static double round1(double v) { return std::floor(v * 10.0 + 0.5) / 10.0; }


DB process_input(std::istream &in) {
    DB db;

    std::string station;
    std::string value;

    std::chrono::duration<double> getline_time{0};
    std::chrono::duration<double> stod_time{0};
    std::chrono::duration<double> find_time{0};
    std::chrono::duration<double> insert_time{0};
    std::chrono::duration<double> update_time{0};

    auto tp = std::chrono::steady_clock::now();

    // Grab the station and the measured value from the input
    while (std::getline(in, station, ';') && std::getline(in, value, '\n')) {
        auto t1 = std::chrono::steady_clock::now();
        getline_time += t1 - tp;

        // Convert the measured value into a floating point
        double fp_value = std::stod(value);
        auto t2 = std::chrono::steady_clock::now();
        stod_time += t2 - t1;

        // Lookup the station in our database
        auto it = db.find(station);
        auto t3 = std::chrono::steady_clock::now();
        find_time += t3 - t2;

        if (it == db.end()) {
            // If it's not there, insert
            db.emplace(station, Record{1, fp_value, fp_value, fp_value});
            auto t4 = std::chrono::steady_clock::now();
            insert_time += t4 - t3;
            tp = t4;
            continue;
        }
        // Otherwise update the information
        it->second.min = std::min(it->second.min, fp_value);
        it->second.max = std::max(it->second.max, fp_value);
        it->second.sum += fp_value;
        ++it->second.cnt;
        auto t4 = std::chrono::steady_clock::now();
        update_time += t4 - t3;
        tp = t4;
    }
    //std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cerr << "  getline: " << getline_time.count() << "s\n";
    std::cerr << "  stod:    " << stod_time.count() << "s\n";
    std::cerr << "  find:    " << find_time.count() << "s\n";
    std::cerr << "  insert:  " << insert_time.count() << "s\n";
    std::cerr << "  update:  " << update_time.count() << "s\n";

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

    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "Failed to open " << path << "\n";
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();
    auto db = process_input(in);
    auto t1 = std::chrono::steady_clock::now();
    format_output(std::cout, db);
    auto t2 = std::chrono::steady_clock::now();

    std::chrono::duration<double> process_input_time = t1 - t0;
    std::chrono::duration<double> format_output_time = t2 - t1;
    std::cerr << "process_input: " << process_input_time.count() << "s\n";
    std::cerr << "format_output: " << format_output_time.count() << "s\n";
}
