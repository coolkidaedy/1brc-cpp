// Fixed-format 1BRC parser. See README.md for assumptions and timing semantics.
#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

constexpr size_t CAPACITY = 1 << 15;
constexpr size_t MAX_LINE = 107;
struct Slot {
    uint64_t w0 = 0, w1 = 0;
    const char *ptr = nullptr;
    int64_t sum = 0;
    uint32_t cnt = 0, len = 0;
    int16_t min = 0, max = 0;
};
static_assert(sizeof(Slot) == 48);
using Table = std::vector<Slot>;

static uint64_t load64(const char *p) {
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    if constexpr (std::endian::native == std::endian::big) v = std::byteswap(v);
    return v;
}

static uint64_t low_bits(uint64_t v, unsigned bits) {
#if defined(__BMI2__) && defined(__x86_64__)
    return _bzhi_u64(v, bits);
#else
    return bits >= 64 ? v : v & ((uint64_t{1} << bits) - 1);
#endif
}

static size_t hash_key(uint64_t a, uint64_t b) {
#if defined(__SSE4_2__) && defined(__x86_64__)
    return _mm_crc32_u64(_mm_crc32_u64(0, a), b) & (CAPACITY - 1);
#else
    uint64_t h = a ^ std::rotl(b, 29);
    h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27; h *= 0x94d049bb133111ebULL;
    return (h ^ (h >> 31)) & (CAPACITY - 1);
#endif
}

static bool matches(const Slot &s, uint64_t a, uint64_t b, const char *p, uint32_t len) {
    if (s.w0 != a || s.w1 != b) return false;
    // 0xff cannot occur in valid UTF-8. Long images reserve byte 15 for it.
    return len <= 16 || (s.len == len && std::memcmp(s.ptr + 15, p + 15, len - 15) == 0);
}

[[gnu::always_inline]] static inline const char *step(const char *p, Table &db) {
    unsigned len;
#if defined(__SSE2__) && defined(__x86_64__)
    unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(p)), _mm_set1_epi8(';'))));
    len = std::countr_zero(mask);
    if (mask == 0) [[unlikely]] {
        len = 16;
        while (p[len] != ';') ++len;
    }
#else
    len = 0;
    while (p[len] != ';') ++len;
#endif
    uint64_t a = load64(p), b = load64(p + 8);
    if (len < 16) [[likely]] {
        a = low_bits(a, len * 8);
        b = low_bits(b, (len - 8) * 8) & (uint64_t{0} - (len >> 3));
    } else if (len > 16) {
        b = (b & 0x00ffffffffffffffULL) | 0xff00000000000000ULL;
    }
    const char *temp = p + len + 1;
    uint64_t word = load64(temp);
    unsigned dot = std::countr_zero(~word & 0x10101000ULL);
    int64_t sign = -static_cast<int64_t>((~word >> 4) & 1);
    uint64_t digits = ((word & ~(static_cast<uint64_t>(sign) & 0xff)) << (28 - dot))
                      & 0x0f000f0f00ULL;
    int v = static_cast<int>((digits * 0x640a0001ULL >> 32) & 0x3ff);
    v = (v ^ static_cast<int>(sign)) - static_cast<int>(sign);
    size_t idx = hash_key(a, b);
    while (db[idx].len && !matches(db[idx], a, b, p, len)) idx = (idx + 1) & (CAPACITY - 1);
    Slot &s = db[idx];
    if (!s.len) [[unlikely]] {
        s = Slot{a, b, p, v, 1, len, static_cast<int16_t>(v), static_cast<int16_t>(v)};
    } else {
        s.sum += v; ++s.cnt;
        if (v < s.min) s.min = static_cast<int16_t>(v);
        if (v > s.max) s.max = static_cast<int16_t>(v);
    }
    return temp + (dot >> 3) + 3;
}

static const char *align_line(const char *p, const char *end) {
    while (p < end && *p != '\n') ++p;
    return p < end ? p + 1 : end;
}

static void process(const char *begin, const char *end, Table &db) {
    const auto size = end - begin;
    const char *e0 = align_line(begin + size / 3, end);
    const char *e1 = align_line(begin + size * 2 / 3, end);
    const char *p0 = begin, *p1 = e0, *p2 = e1;
    // Scalar cursors expose three independent dependency chains to the compiler.
    while (true) {
        auto n = std::min({e0 - p0, e1 - p1, end - p2}) / MAX_LINE;
        if (!n) break;
        do {
            p0 = step(p0, db); p1 = step(p1, db); p2 = step(p2, db);
        } while (--n);
    }
    while (p0 < e0) p0 = step(p0, db);
    while (p1 < e1) p1 = step(p1, db);
    while (p2 < end) p2 = step(p2, db);
}

static void append_tenths(std::string &out, int64_t v) {
    if (v < 0) { out += '-'; v = -v; }
    out += std::to_string(v / 10); out += '.';
    out += static_cast<char>('0' + v % 10);
}

static std::string calculate(const char *path, unsigned num_threads) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open input");
    struct stat st{};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        close(fd); throw std::runtime_error("input must be a regular file");
    }
    size_t size = static_cast<size_t>(st.st_size);
    if (!size) { close(fd); return "{}\n"; }
    size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    size_t rounded = (size + page - 1) / page * page;
    char *data = static_cast<char *>(mmap(nullptr, rounded + page, PROT_READ,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (data == MAP_FAILED) { close(fd); throw std::runtime_error("cannot reserve mapping"); }
    void *mapped = mmap(data, size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) throw std::runtime_error("cannot map input");
    if (data[size - 1] != '\n') throw std::runtime_error("input must end with a newline");
    // Mapping deliberately lives to process exit: table keys point into it.
    num_threads = std::min<unsigned>(num_threads, static_cast<unsigned>(std::min<size_t>(1024, size / 4096 + 1)));
    size_t chunks = num_threads * 24;
    std::vector<const char *> boundaries(chunks + 1);
    boundaries[0] = data; boundaries[chunks] = data + size;
    for (size_t i = 1; i < chunks; ++i)
        boundaries[i] = align_line(data + size / chunks * i, data + size);
    std::vector<Table> tables(num_threads, Table(CAPACITY));
    std::atomic<size_t> next{0};
    std::vector<std::jthread> threads;
    for (unsigned t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t] {
            size_t i;
            while ((i = next.fetch_add(1, std::memory_order_relaxed)) < chunks)
                process(boundaries[i], boundaries[i + 1], tables[t]);
        });
    }
    for (auto &thread : threads) thread.join();
    Table &db = tables[0];
    for (unsigned t = 1; t < num_threads; ++t) {
        for (const Slot &s : tables[t]) {
            if (!s.len) continue;
            size_t idx = hash_key(s.w0, s.w1);
            while (db[idx].len && !matches(db[idx], s.w0, s.w1, s.ptr, s.len)) idx = (idx + 1) & (CAPACITY - 1);
            Slot &d = db[idx];
            if (!d.len) d = s;
            else {
                d.sum += s.sum; d.cnt += s.cnt;
                d.min = std::min(d.min, s.min); d.max = std::max(d.max, s.max);
            }
        }
    }
    std::vector<const Slot *> sorted;
    for (const auto &s : db) if (s.len) sorted.push_back(&s);
    std::sort(sorted.begin(), sorted.end(), [](const Slot *a, const Slot *b) {
        return std::string_view(a->ptr, a->len) < std::string_view(b->ptr, b->len);
    });
    std::string out = "{";
    for (const Slot *s : sorted) {
        if (out.size() > 1) out += ", ";
        out.append(s->ptr, s->len); out += '=';
        append_tenths(out, s->min); out += '/';
        int64_t numerator = 2 * s->sum + s->cnt, denominator = 2LL * s->cnt;
        int64_t mean = numerator / denominator - (numerator % denominator < 0);
        append_tenths(out, mean); out += '/'; append_tenths(out, s->max);
    }
    return out + "}\n";
}

static void write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        ssize_t n = write(fd, bytes.data(), bytes.size());
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("write failed");
        bytes.remove_prefix(static_cast<size_t>(n));
    }
}

int main(int argc, char **argv) {
    try {
        if (argc > 3) throw std::runtime_error("usage: solution5 [file] [threads]");
        unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        if (argc == 3) {
            size_t used = 0;
            std::string arg = argv[2];
            auto n = std::stoul(arg, &used);
            if (used != arg.size() || n < 1 || n > 1024) throw std::runtime_error("threads must be 1..1024");
            threads = static_cast<unsigned>(n);
        }
        const char *path = argc > 1 ? argv[1] : "measurements.txt";
#ifdef NOFORK
        write_all(STDOUT_FILENO, calculate(path, threads));
#else
        int pipefd[2];
        if (pipe(pipefd)) throw std::runtime_error("pipe failed");
        pid_t child = fork();
        if (child < 0) throw std::runtime_error("fork failed");
        if (!child) {
            close(pipefd[0]);
            try {
                std::string result = calculate(path, threads);
                write_all(pipefd[1], result); close(pipefd[1]); _exit(0);
            } catch (const std::exception &e) {
                std::cerr << "solution5: " << e.what() << '\n';
                close(pipefd[1]); _exit(1);
            }
        }
        close(pipefd[1]);
        std::string result;
        char buffer[8192];
        for (;;) {
            ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) throw std::runtime_error("pipe read failed");
            if (!n) break;
            result.append(buffer, static_cast<size_t>(n));
        }
        close(pipefd[0]);
        if (result.size() < 3 || !result.ends_with("}\n")) {
            int status;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error("worker failed before completing output");
        }
        // EOF precedes child address-space teardown. Successful runs do not waitpid.
        write_all(STDOUT_FILENO, result);
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "solution5: " << e.what() << '\n'; return 1;
    }
}
