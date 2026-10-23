// benchmark_win.cpp : shark (your allocator.h) vs the C runtime's malloc  (Windows, also builds on Linux)
//
// Files needed in the same folder:
//     allocator.h        (your original file, NOT modified)
//     sys/mman.h         (small Windows shim: mmap -> VirtualAlloc)
//
// Build (Visual Studio): set configuration to  Release | x64  (Debug numbers are meaningless).
// Build (cmd, "x64 Native Tools Command Prompt"):  cl /O2 /std:c++17 /EHsc benchmark_win.cpp
// Build (MinGW / Linux):                           g++ -std=c++17 -O2 -o benchmark_win benchmark_win.cpp
// Run:  benchmark_win.exe results.csv

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "sys/mman.h"          

#if defined(_MSC_VER)
  #include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
  #include <x86intrin.h>
#endif

#include "allocator.h"     
template <class T> inline void do_not_optimize(T const& v) {
#if defined(_MSC_VER)
    static volatile char sink; sink = *(const volatile char*)&v; _ReadWriteBarrier();
#else
    asm volatile("" : : "r,m"(v) : "memory");
#endif
}

using clk = std::chrono::steady_clock;
static inline double elapsed_ns(clk::time_point a, clk::time_point b) {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
  #define HAVE_TSC 1
  static inline uint64_t ticks() { _mm_lfence(); uint64_t t = __rdtsc(); _mm_lfence(); return t; }
#else
  #define HAVE_TSC 0
  static inline uint64_t ticks() { return (uint64_t)clk::now().time_since_epoch().count(); }
#endif
static double ns_per_tick = 1.0;
static double tick_overhead = 0.0; 

static void calibrate_ticks() {
#if HAVE_TSC
    auto t0 = clk::now(); uint64_t c0 = ticks();
    while (elapsed_ns(t0, clk::now()) < 50e6) {}            // 50 ms
    uint64_t c1 = ticks(); auto t1 = clk::now();
    ns_per_tick = elapsed_ns(t0, t1) / (double)(c1 - c0);
#endif
    std::vector<uint64_t> d(100000);
    for (auto& x : d) { uint64_t a = ticks(); uint64_t b = ticks(); x = b - a; }
    std::sort(d.begin(), d.end());
    tick_overhead = (double)d[d.size() / 2];
}


struct Shark {
    static const char* name() { return "shark"; }
    static void* alloc(size_t n) { return mymalloc(n); }
    static void  release(void* p) { myfree(p); }
    static void  reset() {
        if (pool_start) munmap(pool_start, 1024 * 1024);
        init_pool(); free_list = nullptr;
    }
};
struct Crt {
    static const char* name() { return "malloc"; }
    static void* alloc(size_t n) { return std::malloc(n); }
    static void  release(void* p) { std::free(p); }
    static void  reset() {}
};


template <class A> bool selftest() {
    A::reset();
    const int n = 200;
    std::vector<unsigned char*> p(n); std::vector<size_t> sz(n);
    std::mt19937 rng(1);
    for (int i = 0; i < n; i++) {
        sz[i] = 1 + rng() % 200;
        p[i] = (unsigned char*)A::alloc(sz[i]);
        if (!p[i] || ((uintptr_t)p[i] % 8) != 0) return false;
        std::memset(p[i], (i & 0xFF) | 1, sz[i]);
    }
    for (int i = 0; i < n; i++)                                           
        for (size_t b = 0; b < sz[i]; b++) if (p[i][b] != ((i & 0xFF) | 1)) return false;
    for (int i = 0; i < n; i += 2) A::release(p[i]);
    for (int i = 0; i < n; i += 2) { p[i] = (unsigned char*)A::alloc(sz[i]); if (!p[i]) return false; std::memset(p[i], (i & 0xFF) | 1, sz[i]); }
    for (int i = 0; i < n; i++)
        for (size_t b = 0; b < sz[i]; b++) if (p[i][b] != ((i & 0xFF) | 1)) return false;
    for (int i = 0; i < n; i++) A::release(p[i]);
    return true;
}

template <class A> long w_pair(size_t size, int iters) {
    for (int i = 0; i < iters; i++) { void* p = A::alloc(size); do_not_optimize(p); A::release(p); }
    return iters;
}
template <class A> long w_mixed(int iters) {
    static const size_t s[] = {8, 16, 64, 128, 256, 512};
    for (int i = 0; i < iters; i++) { void* p = A::alloc(s[i % 6]); do_not_optimize(p); A::release(p); }
    return iters;
}
template <class A> long w_bulk(int n, bool reverse) {       
    std::vector<void*> v(n);
    for (int i = 0; i < n; i++) { v[i] = A::alloc(16); do_not_optimize(v[i]); }
    if (reverse) for (int i = n - 1; i >= 0; i--) A::release(v[i]);
    else         for (int i = 0; i < n; i++)      A::release(v[i]);
    return n;
}
template <class A> long w_random_free(int n) {
    std::vector<void*> v(n);
    for (int i = 0; i < n; i++) { v[i] = A::alloc(16); do_not_optimize(v[i]); }
    std::mt19937 rng(42); std::shuffle(v.begin(), v.end(), rng);
    for (int i = 0; i < n; i++) A::release(v[i]);
    return n;
}
template <class A> long w_churn(int live, int iters) {
    static const size_t s[] = {16, 32, 64, 128};
    std::vector<void*> v(live); std::mt19937 rng(7);
    for (int i = 0; i < live; i++) v[i] = A::alloc(s[rng() % 4]);
    for (int i = 0; i < iters; i++) {
        int k = rng() % live;
        A::release(v[k]);
        v[k] = A::alloc(s[rng() % 4]);
        do_not_optimize(v[k]);
    }
    return iters;
}

constexpr int RUNS = 11;
struct Cell { double med = 0, min = 0; bool ok = false; };
static std::vector<std::string> order;
static std::map<std::string, std::map<std::string, Cell>> table;
static bool enabled_shark = true;

static double median_of(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

template <class A, class F> void report(const std::string& test, F body) {
    std::vector<double> r;
    for (int i = 0; i < RUNS + 1; i++) {
        A::reset();
        auto t0 = clk::now(); long ops = body(); auto t1 = clk::now();
        if (i > 0) r.push_back(elapsed_ns(t0, t1) / (double)ops);
    }
    table[test][A::name()] = { median_of(r), *std::min_element(r.begin(), r.end()), true };
}

template <class W> void run_test(const std::string& name, W wl) {
    std::printf("running: %s\n", name.c_str()); std::fflush(stdout);
    order.push_back(name);
    if (enabled_shark) report<Shark>(name, [&] { return wl(Shark{}); });
    report<Crt>(name, [&] { return wl(Crt{}); });
}

template <class A> void latency(std::vector<std::string>& lines, std::map<std::string, std::vector<double>>& csvrows,
                                int live, int iters) {
    static const size_t s[] = {16, 32, 64, 128};
    A::reset();
    std::vector<void*> v(live); std::mt19937 rng(7);
    for (int i = 0; i < live; i++) v[i] = A::alloc(s[rng() % 4]);
    std::vector<double> lat; lat.reserve(iters);
    for (int i = 0; i < iters; i++) {
        int k = rng() % live; A::release(v[k]);
        size_t sz = s[rng() % 4];
        uint64_t t0 = ticks(); void* p = A::alloc(sz); uint64_t t1 = ticks();
        do_not_optimize(p); v[k] = p;
        double c = (double)(t1 - t0) - tick_overhead; if (c < 0) c = 0;
        lat.push_back(c * ns_per_tick);
    }
    std::sort(lat.begin(), lat.end());
    auto P = [&](double q) { return lat[(size_t)(q * (lat.size() - 1))]; };
    char buf[200];
    std::snprintf(buf, sizeof buf, "  %-9s p50 %8.0f   p99 %8.0f   p99.9 %9.0f   max %11.0f", A::name(), P(.5), P(.99), P(.999), lat.back());
    lines.push_back(buf);
    csvrows[A::name()] = { P(.5), P(.99), P(.999), lat.back() };
}

int main(int argc, char** argv) {
    FILE* csv = argc > 1 ? std::fopen(argv[1], "w") : nullptr;
    calibrate_ticks();
    std::printf("timer: %.3f ns/tick, counter overhead %.1f ticks (subtracted from latency samples)\n", ns_per_tick, tick_overhead);

    if (!selftest<Shark>()) { std::printf("SELFTEST FAILED: shark (excluded)\n"); enabled_shark = false; }
    else std::printf("selftest passed: shark\n");

    const int N = 1000000;
    run_test("pair 16B alloc+free",      [&](auto a) { return w_pair<decltype(a)>(16, N); });
    run_test("mixed sizes 8-512B",       [&](auto a) { return w_mixed<decltype(a)>(N); });
    run_test("bulk 1000, free in order", [&](auto a) { return w_bulk<decltype(a)>(1000, false); });
    run_test("bulk 1000, free reversed", [&](auto a) { return w_bulk<decltype(a)>(1000, true); });
    run_test("bulk 5000, random free",   [&](auto a) { return w_random_free<decltype(a)>(5000); });
    run_test("churn, 2000 live",         [&](auto a) { return w_churn<decltype(a)>(2000, 200000); });

    //table of median ns/op
    std::printf("\n=== median ns per operation (lower is better) ===\n");
    std::printf("%-28s %12s %12s %14s\n", "test", "shark", "malloc", "shark vs malloc");
    if (csv) std::fprintf(csv, "test,allocator,median_ns_per_op,min_ns_per_op\n");
    for (auto& t : order) {
        auto& row = table[t];
        double sh = row["shark"].med, cm = row["malloc"].med;
        std::printf("%-28s %12.2f %12.2f %14.2fx\n", t.c_str(), sh, cm, sh > 0 ? cm / sh : 0.0);
        if (csv) for (auto& kv : row) std::fprintf(csv, "%s,%s,%.3f,%.3f\n", t.c_str(), kv.first.c_str(), kv.second.med, kv.second.min);
    }
    std::printf("(\"shark vs malloc\" = how many times faster shark is than malloc; below 1.00x means shark is slower)\n");

    // latency percentiles
    std::printf("\n=== alloc latency in ns during churn, 2000 live blocks ===\n");
    std::vector<std::string> lines; std::map<std::string, std::vector<double>> rows;
    if (enabled_shark) latency<Shark>(lines, rows, 2000, 200000);
    latency<Crt>(lines, rows, 2000, 200000);
    for (auto& l : lines) std::printf("%s\n", l.c_str());
    if (csv) {
        std::fprintf(csv, "\nlatency_churn,allocator,p50_ns,p99_ns,p99.9_ns,max_ns\n");
        for (auto& kv : rows) std::fprintf(csv, "latency_churn,%s,%.0f,%.0f,%.0f,%.0f\n", kv.first.c_str(), kv.second[0], kv.second[1], kv.second[2], kv.second[3]);
        std::fclose(csv);
    }
    return 0;
}