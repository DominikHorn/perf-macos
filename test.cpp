#include "perf-macos.hpp"

#include <iostream>
#include <string>

void basic_usage() {
    const uint64_t n = 1000000;
    uint64_t acc = 0x0;

    // Initialize counter. This will take care of setting everything up for perf measurements
    Perf::Counter counter;

    // Start measuring
    counter.start();

    // Code to benchmark
    for (uint64_t i = 0; i < n; i++) { acc ^= i * 0xABCDEF010; }

    // Stop measuring
    auto measurement = counter.stop();

    // Average measurements for our iterations and pretty print
    measurement.averaged(n).pretty_print();

    // Lay mans hack to prevent compiler elimination of acc computation.
    // For advanced method, see, e.g., https://github.com/google/benchmark/blob/376ebc26354ca2b79af94467133f3c35b539627e/include/benchmark/benchmark.h#L325
    std::cout << acc << std::endl;
}

void block_counter() {
    const uint64_t n = 1000000;
    uint64_t acc = 0x0;

    {
        // This will automatically start() after construction and stop() on destruction
        Perf::BlockCounter b(n);

        // Code to benchmark
        for (uint64_t i = 0; i < n; i++) { acc ^= i * 0xABCDEF010; }
    }

    // Lay mans hack to prevent compiler elimination of acc computation.
    // For advanced method, see, e.g., https://github.com/google/benchmark/blob/376ebc26354ca2b79af94467133f3c35b539627e/include/benchmark/benchmark.h#L325
    std::cout << acc << std::endl;
}

int main() {
    basic_usage();
    block_counter();

    return 0;
}