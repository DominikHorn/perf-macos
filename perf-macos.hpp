/**
 * Copyright 2021 Dominik Horn
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PERF_MACOS_HPP
#define PERF_MACOS_HPP

#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * =====================
 *    Arch detection
 * =====================
 */

/* CPU(X86_64) - AMD64 / Intel64 / x86_64 64-bit */
#if defined(__x86_64__) || defined(_M_X64)
#define CPU_X86_64 1
#endif

/* CPU(ARM64) - Apple */
#if (defined(__arm64__) && defined(__APPLE__)) || defined(__aarch64__)
#define CPU_ARM64 1
#endif

/**
 * =====================
 *   Compiler builtins
 * =====================
 */
#ifdef NDEBUG
#ifdef __GNUC__
#define forceinline inline __attribute__((always_inline))

#else
#warning "perf-macos currently does not fully support your compiler"
#define forceinline

#endif
#else
#define forceinline
#endif

/**
 * ====================
 *   Helper Defines
 * ====================
 */
#define KPC_CLASS_FIXED_MASK (1u << 0)
#define KPC_CLASS_CONFIGURABLE_MASK (1u << 1)
#define KPC_CLASS_POWER_MASK (1u << 2)
#define KPC_CLASS_RAWPMU_MASK (1u << 3)

/// Fixed counters are not properly implemented in XNU (https://github.com/apple/darwin-xnu/blob/8f02f2a044b9bb1ad951987ef5bab20ec9486310/osfmk/x86_64/kpc_x86.c#L316),
/// i.e., they always count (ring 0 - 3)
#ifdef CPU_X86_64
#define KPC_CLASSES_MASK KPC_CLASS_CONFIGURABLE_MASK
#else
#define KPC_CLASSES_MASK (KPC_CLASS_CONFIGURABLE_MASK | KPC_CLASS_FIXED_MASK)
#endif

#define KPERF_FRAMEWORK_PATH "/System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf"

// Available kperf functions
#define KPERF_FUNCTIONS_LIST                                                                                           \
    KPERF_FUNC(kpc_force_all_ctrs_set, int, int)                                                                       \
    KPERF_FUNC(kpc_get_config, int, uint32_t, void *)                                                                  \
    KPERF_FUNC(kpc_get_config_count, uint32_t, uint32_t)                                                               \
    KPERF_FUNC(kpc_get_counter_count, uint32_t, uint32_t)                                                              \
    KPERF_FUNC(kpc_get_counting, int, void)                                                                            \
    KPERF_FUNC(kpc_get_period, int, uint32_t, void *)                                                                  \
    KPERF_FUNC(kpc_get_thread_counters, int, int, unsigned int, void *)                                                \
    KPERF_FUNC(kpc_set_config, int, uint32_t, void *)                                                                  \
    KPERF_FUNC(kpc_set_counting, int, uint32_t)                                                                        \
    KPERF_FUNC(kpc_set_period, int, uint32_t, void *)                                                                  \
    KPERF_FUNC(kpc_set_thread_counting, int, uint32_t)                                                                 \
    KPERF_FUNC(kperf_sample_get, int, int *)

#define KPC_ERROR(msg) throw std::runtime_error(msg ". Did you forget to run as root?")

// TODO: convert this to enum (?)
#ifdef CPU_X86_64
#define CORE_CYCLES 0x003C
#define REFERENCE_CYCLES 0x013C
#define INSTRUCTIONS_RETIRED 0x00C0
#define BRANCH_INSTRUCTION_RETIRED 0x00C4
#define BRANCH_MISSES_RETIRED 0x00C5
#define LLC_REFERENCES 0x4F2E
#define LLC_MISSES 0x412E
#define L1_MISSES 0x01CB
#define L2_MISSES 0x04CB
#elif defined(CPU_ARM64)
#error "arm64 not fully implemented"
#endif
/**
 * ====================
 *    Implementation
 * ====================
 */
struct PerfCounter {
    /**
     * Initialize a perf counter. For convenience, you may specify a quality of
     * service class for the current thread. PerfCounter will take care of
     * setting up the thread such that it is scheduled with the given qos.
     * For the default value, QOS_CLASS_USER_INTERACTIVE, this should pin
     * this thread to a high performance core for big/little CPUs.
     * See https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheTaskLevel.html
     * for more details.
     *
     * @param qos_class quality of service to set for the current thread.
     *        Default is QOS_CLASS_USER_INTERACTIVE.
     */
    explicit PerfCounter(const uint64_t repetition_cnt, const qos_class_t qos_class = QOS_CLASS_USER_INTERACTIVE)
        : repetition_cnt(repetition_cnt), desired_counters({{INSTRUCTIONS_RETIRED, "instructions"},
                                                            {L1_MISSES, "L1 misses"},
                                                            {LLC_MISSES, "LLC misses"},
                                                            {BRANCH_MISSES_RETIRED, "branch misses"},
                                                            {CORE_CYCLES, "cycles"},
                                                            {BRANCH_INSTRUCTION_RETIRED, "branches"}}) {
        // Load kperf to communicate with XNU api
        load_kperf();

        // Setup once to
        CTRS_CNT = kpc_get_counter_count(KPC_CLASSES_MASK);
        counters = new uint64_t[CTRS_CNT];

        // set thread qos (for little/big architectures, this will decide which
        // core this thread is scheduled on)
        pthread_set_qos_class_self_np(qos_class, 0);

        // Ensure counters are properly setup
        configure_counters();

        // initial measurement
        start = read_counter_values();
    }

    ~PerfCounter() {
        // Measure counter delta (TODO: Deal with overflows?)
        const auto end = read_counter_values();

        // Print results
        std::cout << "averages after " << std::dec << repetition_cnt << " repetitions: " << std::endl;
        const auto column_width = 15;
        for (const auto &it : desired_counters) { std::cout << std::setw(column_width) << it.second; }
        std::cout << std::endl;
        for (const auto &it : desired_counters) {
            const auto start_val = start.find(it.second);
            const auto end_val = end.find(it.second);
            std::cout << std::setw(column_width)
                      << (start_val == start.end() || end_val == end.end()
                                  ? "-"
                                  : std::to_string(static_cast<long double>(end_val->second - start_val->second) /
                                                   static_cast<long double>(repetition_cnt)));
        }
        std::cout << std::endl;

        delete counters;
    }

private:
    uint64_t repetition_cnt;
    std::unordered_map<std::string, uint64_t> start;
    std::vector<std::pair<uint64_t, std::string>> desired_counters;

    uint32_t CTRS_CNT;
    uint64_t *counters;

// Define kperf functions for later access
#define KPERF_FUNC(func_sym, return_value, ...)                                                                        \
    typedef return_value func_sym##_type(__VA_ARGS__);                                                                 \
    func_sym##_type *func_sym;
    KPERF_FUNCTIONS_LIST
#undef KPERF_FUNC

    forceinline void load_kperf() {
        // Load kperf code into adress space. Only do this once (`man dlopen` did not quite promise idempotency)
        static void *kperf = dlopen(KPERF_FRAMEWORK_PATH, RTLD_LAZY);
        if (!kperf) { throw std::runtime_error(std::string("Unable to load kperf: ").append(dlerror())); }

        // obtain pointers to kperf functions
#define KPERF_FUNC(func_sym, return_value, ...)                                                                        \
    func_sym = (func_sym##_type *) (dlsym(kperf, #func_sym));                                                          \
    if (!func_sym) {                                                                                                   \
        throw std::runtime_error(std::string("kperf missing symbol: " #func_sym ": ").append(dlerror()));              \
        return;                                                                                                        \
    }
        KPERF_FUNCTIONS_LIST
#undef KPERF_FUNC
    };

    forceinline std::unordered_map<std::string, uint64_t> read_counter_values() const {
        // Obtain counters for current thread
        if (kpc_get_thread_counters(0, CTRS_CNT, counters)) { KPC_ERROR("Failed to read current kpc config"); }

        std::unordered_map<std::string, uint64_t> counter_values{};
        for (size_t i = 0; i < CTRS_CNT && i < desired_counters.size(); i++) {
            counter_values.emplace(desired_counters[i].second, counters[i]);
        }
        return counter_values;
    }

    forceinline void configure_counters() {
        auto configs_cnt = kpc_get_config_count(KPC_CLASSES_MASK);
        uint64_t configs[configs_cnt];

#ifdef CPU_X86_64
        /**
         * For documentation, see:
         * - Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3, Section 18.2.1.1.
         * - https://github.com/apple/darwin-xnu/blob/8f02f2a044b9bb1ad951987ef5bab20ec9486310/osfmk/x86_64/kpc_x86.c#L348
         */
        const uint64_t INTEL_CONF_CTR_USER_MODE = 0x10000;
        //        const uint64_t INTEL_CONF_CTR_OS_MODE = 0x20000;
        //        const uint64_t INTEL_CONF_CTR_EDGE_DETECT = 0x40000;
        //        const uint64_t INTEL_CONF_CTR_ENABLED = 0x200000;
        //        const uint64_t INTEL_CONF_CTR_INVERTED = 0x400000;
        //        const auto INTEL_CONF_CTR_CMASK = [](const uint8_t cmask) { return (cmask & 0xFF) << 24; };

        for (size_t i = 0; i < configs_cnt; i++) {
            if (i >= desired_counters.size()) {
                std::cerr << "[PerfCounter] More configurable perf registers are available than were selected"
                          << std::endl;
                break;
            }

            // TODO: see if we actually need to set USER_MODE ourselfs
            configs[i] = (0xFFFF & desired_counters[i].first) | INTEL_CONF_CTR_USER_MODE;
        }
#elif defined(CPU_ARM64)
#error "arm64 not fully implemented"
#endif

        // set config
        if (kpc_set_config(KPC_CLASSES_MASK, configs)) { KPC_ERROR("Could not configure counters"); }

        // TODO: figure out what this is supposed to do. Best guess: ARM specific, not needed for intel (NOOP in this case)
        // https://github.com/apple/darwin-xnu/blob/8f02f2a044b9bb1ad951987ef5bab20ec9486310/osfmk/kern/kpc.h#L181
        if (kpc_force_all_ctrs_set(1)) { KPC_ERROR("Could not force ctrs"); }

        if (kpc_set_counting(KPC_CLASSES_MASK) || kpc_set_thread_counting(KPC_CLASSES_MASK)) {
            KPC_ERROR("Failed to enable counting");
        }
    }
};

/**
 * ===================
 *       Undefs
 * ===================
 */
#undef CPU_X86_64
#undef CPU_ARM64

#undef forceinline

#undef KPC_CLASS_FIXED_MASK
#undef KPC_CLASS_CONFIGURABLE_MASK
#undef KPC_CLASS_POWER_MASK
#undef KPC_CLASS_RAWPMU_MASK

#undef KPERF_FRAMEWORK_PATH
#undef KPERF_FUNCTIONS_LIST

#undef INTEL_UNHALTED_CORE_CYCLES
#undef INTEL_INSTRUCTIONS_RETIRED
#undef INTEL_UNHALTED_REFERENCE_CYCLES
#undef INTEL_LLC_REFERENCES
#undef INTEL_LLC_MISSES
#undef INTEL_L1_MISSES
#undef INTEL_L2_MISSES
#undef INTEL_BRANCH_INSTRUCTION_RETIRED
#undef INTEL_BRANCH_MISSES_RETIRED

#endif
