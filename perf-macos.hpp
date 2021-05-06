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

#include <chrono>
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

#define PERF_ERROR(msg) throw std::runtime_error(msg ". Did you forget to run as root?")

/**
 * ====================
 *    Implementation
 * ====================
 */
namespace Perf {
    enum Event {
#ifdef CPU_X86_64
        instructions_retired = 0x00C0,
        l1_misses = 0x01CB,
        llc_misses = 0x412E,
        branch_misses_retired = 0x00C5,
        cycles = 0x003C,
        branch_instruction_retired = 0x00C4,
        l2_misses = 0x04CB,
        llc_references = 0x4F2E,
        reference_cycles = 0x013C,
#elif defined(CPU_ARM64)
#error "arm64 not fully implemented"
#endif
    };

    /**
     * To ensure stable measurements, it is advisable to set thread quality
     * of service. Especially for big/little CPUs, this can help ensuring that
     * the code always executes on a high performance core.
     * See https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheTaskLevel.html
     * for more details.
     *
     * @param qos_class quality of service to set for the current thread.
     *        Default is QOS_CLASS_USER_INTERACTIVE.
     */
    [[maybe_unused]] static forceinline void set_thread_qos(const qos_class_t qos_class = QOS_CLASS_USER_INTERACTIVE) {
        // set thread qos (for little/big architectures, this will decide which
        // core this thread is scheduled on)
        pthread_set_qos_class_self_np(qos_class, 0);
    }

    /// A Perf::Measurement captures the values of perf hardware counters at a specific point in time
    template<class D = uint64_t>
    struct Measurement {
        const std::unordered_map<Event, D> data;
        const long double time_delta_ns;

        Measurement(const std::unordered_map<Event, D> &data, const long double &time_delta_ns)
            : data(data), time_delta_ns(time_delta_ns) {}

        /**
         * Pretty print this measurement in a one-row table (with header).
         *
         * @param column_width width (in chars) of each table column
         */
        void pretty_print(unsigned int column_width = 15) const {
            // Table header
            std::cout << std::setw(column_width) << "Elapsed [ns]";
            for (const auto &it : data) { std::cout << std::setw(column_width) << human_readable_name(it.first); }
            std::cout << std::endl;

            // Table row
            std::cout << std::setw(column_width) << std::to_string(time_delta_ns);
            for (const auto &it : data) { std::cout << std::setw(column_width) << std::to_string(it.second); }
            std::cout << std::endl;
        }

        /**
         * Divide each measured datapoint by N, effectively obtaining
         * an average figure for the benchmarked code within the N-step
         * benchmark repeat loop.
         *
         * @tparam T type of N, e.g., uint64_t
         * @tparam R result type, defaults to long double (precision)
         * @param N divisor, meant to be set to amount of iterations of the benchmark repeat loop
         * @return
         */
        template<class T, class R = long double>
        Measurement<R> averaged(const T &N) const {
            std::unordered_map<Event, R> new_data;
            for (const auto &it : data) { new_data.emplace(it.first, static_cast<R>(it.second) / static_cast<R>(N)); }
            return Measurement<R>(new_data, time_delta_ns / static_cast<long double>(N));
        }

    private:
        static std::string human_readable_name(const Event &event) {
            switch (event) {
                case instructions_retired:
                    return "Instructions";
                case l1_misses:
                    return "L1 misses";
                case llc_misses:
                    return "LLC misses";
                case branch_misses_retired:
                    return "Branch misses";
                case cycles:
                    return "Cycles";
                case branch_instruction_retired:
                    return "Branches";
                case l2_misses:
                    return "L2 misses";
                case llc_references:
                    return "LLC misses";
                case reference_cycles:
                    return "Reference cycles";
                default:
                    return "Unimplemented";
            }
        }
    };

    /**
     * Perf::Counter retrieves perf hardware counter
     * values at given points in time.
     *
     * Please note that perf counters seem to have a precision lower
     * bound, afaik to prohibit some malicious use-cases. Also note
     * that even though care was taken to minimize the amount of
     * overhead, starting/stopping measurements will slightly
     * skew counter values. It is therefore advisable to iterate
     * multiple times over your benchmark and then average
     * the resulting measurement.
     */
    struct Counter {
    private:
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

    public:
        /**
         * Initialize a counter, optionally specifying which events to measure.
         * On systems with fewer perf counter registers than requested counter
         * @param measured_events
         */
        Counter(std::vector<Event> measured_events = {instructions_retired, l1_misses, llc_misses,
                                                      branch_misses_retired, cycles, branch_instruction_retired})
            : measured_events(measured_events) {
            // Load kperf to communicate with XNU api
            load_kperf();

            // Setup once to
            _counters_size = kpc_get_counter_count(KPC_CLASSES_MASK);
            start_counters = new uint64_t[_counters_size];
            stop_counters = new uint64_t[_counters_size];
        }

        ~Counter() {
            teardown_counters();

            delete start_counters;
            delete stop_counters;
        }

        /**
         * Starts measuring according to configuration. Designed
         * to induce least amount of overhead possible so that
         * only your code is benchmarked.
         *
         * Please note that perf counters seem to have a precision lower
         * bound, afaik to prohibit some malicious use-cases. Also note
         * that even though care was taken to minimize the amount of
         * overhead, starting/stopping measurements will slightly
         * skew counter values. It is therefore advisable to iterate
         * multiple times over your benchmark and then average
         * the resulting measurement.
         */
        forceinline void start() {
            // Setup counters according to our configuration
            configure_counters();
            start_time = std::chrono::steady_clock::now();
            read_counters(start_counters);
        }

        /**
         * Stops measuring and afterwards, computes elapsed counter
         * deltas since last start() invocation. Designed to induce
         * the least amount of overhead possible so that only your
         * code is benchmarked.
         *
         * Please note that perf counters seem to have a precision lower
         * bound, afaik to prohibit some malicious use-cases. Also note
         * that even though care was taken to minimize the amount of
         * overhead, starting/stopping measurements will slightly
         * skew counter values. It is therefore advisable to iterate
         * multiple times over your benchmark and then average
         * the resulting measurement.
         *
         * @return elapsed counter deltas since last start() invocation
         */
        forceinline Measurement<uint64_t> stop() {
            read_counters(stop_counters);
            const auto end_time = std::chrono::steady_clock::now();

            std::unordered_map<Event, uint64_t> counter_values{};
            for (size_t i = 0; i < std::min(_counters_size, measured_events.size()); i++) {
                // TODO: deal with overflow in counter registers (automagically handled by xnu/kperf?)
                counter_values.emplace(measured_events[i], stop_counters[i] - start_counters[i]);
            }
            return Measurement(counter_values, (end_time - start_time).count());
        }

    private:
        std::vector<Event> measured_events;

        size_t _counters_size;
        uint64_t *start_counters;
        uint64_t *stop_counters;
        std::chrono::time_point<std::chrono::steady_clock> start_time;

        forceinline void read_counters(uint64_t *counters) const {
            // Obtain counters for current thread
            if (kpc_get_thread_counters(0, _counters_size, counters)) {
                PERF_ERROR("Failed to read current kpc config");
            }
        }

        forceinline void configure_counters() {
            auto configs_cnt = kpc_get_config_count(KPC_CLASSES_MASK);
            uint64_t configs[configs_cnt];

#ifdef CPU_X86_64
            /**
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
                if (i >= measured_events.size()) {
                    std::cout << "[Perf::Counter] More configurable perf registers are available than were selected"
                              << std::endl;
                    break;
                }

                configs[i] = (0xFFFF & measured_events[i]) | INTEL_CONF_CTR_USER_MODE;
            }
#elif defined(CPU_ARM64)
#error "arm64 not fully implemented"
#endif

            // set config
            if (kpc_set_config(KPC_CLASSES_MASK, configs)) { PERF_ERROR("Could not configure counters"); }

            // TODO: figure out what this is supposed to do. Best guess: ARM specific, not needed for intel (appears to be NOOP in this case)
            // https://github.com/apple/darwin-xnu/blob/8f02f2a044b9bb1ad951987ef5bab20ec9486310/osfmk/kern/kpc.h#L181
            if (kpc_force_all_ctrs_set(1)) { PERF_ERROR("Could not force ctrs"); }

            if (kpc_set_counting(KPC_CLASSES_MASK) || kpc_set_thread_counting(KPC_CLASSES_MASK)) {
                PERF_ERROR("Failed to enable counting");
            }
        }

        forceinline void teardown_counters() {
            // TODO check whether this is enough (and whether we even need to do this or not)
            if (kpc_force_all_ctrs_set(0)) { PERF_ERROR("Could not unforce counters"); }
        }
    };

    /**
     * Scope-based convenience wrapper for Counter.
     *
     * Will automatically invoke start() as last
     * action in constructor and stop(), averaged(N)
     * and pretty_print() when it goes out of scope
     */
    struct BlockCounter : public Counter {
        /**
         * Construct a BlockCounter
         *
         * Will automatically invoke start() as last
         * action in constructor and stop(), averaged(N)
         * and pretty_print() when it goes out of scope
         *
         * @param N the number of iterations of the benchmark-repeat loop.
         *  Measurements will be divided by N. The printed statistics therefore
         *  effectively represent the average for the benchmark-repeat inner body
         * @param measured_events
         */
        BlockCounter(const size_t N,
                     std::vector<Event> measured_events = {instructions_retired, l1_misses, llc_misses,
                                                           branch_misses_retired, cycles, branch_instruction_retired})
            : Counter(measured_events), N(N) {
            start();
        }

        ~BlockCounter() {
            auto measurement = stop();
            measurement.averaged(N).pretty_print();
        }

    private:
        const size_t N;
    };
}// namespace Perf

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
#undef KPC_CLASSES_MASK

#undef KPERF_FRAMEWORK_PATH
#undef KPERF_FUNCTIONS_LIST

#undef PERF_ERROR

#endif
