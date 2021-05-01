#include "perf-macos.hpp"

#include <iostream>
#include <string>

int main() {
    const uint64_t test_rep_cnts[] = {1000000};
    uint64_t acc = 0x0;

    for (const auto test_rep_cnt : test_rep_cnts) {
        std::cout << "repetitions: " << std::dec << test_rep_cnt << std::endl;

        // This will kickoff measuring and stop as soon as it's out of scope
        const auto ctr = PerfCounter(test_rep_cnt);

        // Code to benchmark
        for (uint64_t i = 0; i < test_rep_cnt; i++) { acc ^= i * 0xABCDEF010; }
    }

    std::cout << acc << std::endl;

    return 0;
}