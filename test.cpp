#include "perf-macos.hpp"

#include <iostream>
#include <string>

// https://www.youtube.com/watch?v=nXaxk27zwlk&t=2441s, improved version from
// https://github.com/google/benchmark/blob/ba9a763def4eca056d03b1ece2946b2d4ef6dfcb/include/benchmark/benchmark.h#L326
#define DoNotEliminate(value) asm volatile("" : : "r,m"(value) : "memory")

void basic_usage() {
    const uint64_t n = 1000000;
    uint64_t acc = 0x0;

    // Initialize counter. This will take care of setting everything up for perf measurements
    Perf::Counter counter;

    // Start measuring
    counter.start();

    // Code to benchmark. Iterated n-times to get accurate measurements
    for (uint64_t i = 0; i < n; i++) {
        acc += 0xABCDEF03 / (i + 1);
        DoNotEliminate(acc);
    }

    // Stop measuring
    auto measurement = counter.stop();

    // Average measurements for our iterations and pretty print
    measurement.averaged(n).pretty_print();
}

void block_counter() {
    const uint64_t n = 1000000;
    uint64_t acc = 0x0;

    {
        // This will automatically start() after construction and stop() on destruction
        Perf::BlockCounter b(n);

        // Code to benchmark. Iterated n-times to get accurate measurements
        for (uint64_t i = 0; i < n; i++) {
            acc += i;
            DoNotEliminate(acc);
        }
    }
}

int main() {
    basic_usage();
    block_counter();

    return 0;
}