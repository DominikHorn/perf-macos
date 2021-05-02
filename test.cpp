#include "perf-macos.hpp"

#include <iostream>
#include <string>

int main() {
    const uint64_t n = 1000000;
    uint64_t acc = 0x0;

    // Initialize counter. This will take care of setting everything up for perf measurements
    Perf::Counter counter;

    // Start measuring
    counter.start();

    // Code to benchmark
    {
        for (uint64_t i = 0; i < n; i++) { acc ^= i * 0xABCDEF010; }
    }

    // Stop measuring
    auto measurement = counter.stop();

    // Average measurements for our iterations and pretty print
    measurement.averaged(n).pretty_print();

    // Lay mans hack to prevent compiler elimination of acc computation
    std::cout << acc << std::endl;

    return 0;
}