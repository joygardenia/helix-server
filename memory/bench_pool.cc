#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "../include/memoryPool.h"

// 每线程分配/释放次数
static constexpr int OPS_PER_THREAD = 500000;

struct BenchResult {
    double elapsed_ms;
    long   ops;
};

// ---- malloc/free ----
BenchResult bench_malloc(int nthreads, size_t size) {
    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < nthreads; ++t) {
        threads.emplace_back([size]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                void* p = malloc(size);
                // 防止编译器优化掉
                reinterpret_cast<char*>(p)[0] = 1;
                free(p);
            }
        });
    }
    for (auto& th : threads) th.join();

    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return {ms, (long)nthreads * OPS_PER_THREAD};
}

// ---- 内存池 ----
BenchResult bench_pool(int nthreads, size_t size) {
    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < nthreads; ++t) {
        threads.emplace_back([size]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                void* p = memoryPool::HashBucket::useMemory(size);
                reinterpret_cast<char*>(p)[0] = 1;
                memoryPool::HashBucket::freeMemory(p, size);
            }
        });
    }
    for (auto& th : threads) th.join();

    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return {ms, (long)nthreads * OPS_PER_THREAD};
}

void run(int nthreads, size_t size) {
    auto r_malloc = bench_malloc(nthreads, size);
    auto r_pool   = bench_pool(nthreads, size);

    double malloc_ops = r_malloc.ops / r_malloc.elapsed_ms * 1000;
    double pool_ops   = r_pool.ops   / r_pool.elapsed_ms   * 1000;

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "size=" << std::setw(4) << size << "B  threads=" << nthreads
              << "  malloc: " << std::setw(10) << (long)malloc_ops << " ops/s  "
              << std::setw(7) << r_malloc.elapsed_ms << "ms"
              << "  |  pool: " << std::setw(10) << (long)pool_ops << " ops/s  "
              << std::setw(7) << r_pool.elapsed_ms << "ms"
              << "  speedup: " << std::setprecision(2) << pool_ops / malloc_ops << "x"
              << "\n";
}

int main() {
    memoryPool::HashBucket::initMemoryPool();

    std::cout << "每线程操作数: " << OPS_PER_THREAD << "\n\n";

    // 不同对象大小
    for (size_t sz : {32, 64, 128, 256}) {
        for (int nt : {1, 4, 8}) {
            run(nt, sz);
        }
        std::cout << "\n";
    }
    return 0;
}
