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
#include <stdexcept>
#include <string>

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
#define KPERF_FRAMEWORK_PATH \
    "/System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf"

// Available kperf functions
#define KPERF_FUNCTIONS_LIST                                            \
    KPERF_FUNC(kpc_force_all_ctrs_set, int, int)                        \
    KPERF_FUNC(kpc_get_config, uint32_t, int, void *)                   \
    KPERF_FUNC(kpc_get_config_count, uint32_t, uint32_t)                \
    KPERF_FUNC(kpc_get_counter_count, uint32_t, uint32_t)               \
    KPERF_FUNC(kpc_get_counting, int, void)                             \
    KPERF_FUNC(kpc_get_period, uint32_t, int, void *)                   \
    KPERF_FUNC(kpc_get_thread_counters, int, int, unsigned int, void *) \
    KPERF_FUNC(kpc_set_config, uint32_t, int, void *)                   \
    KPERF_FUNC(kpc_set_counting, int, uint32_t)                         \
    KPERF_FUNC(kpc_set_period, int, uint32_t, void *)                   \
    KPERF_FUNC(kpc_set_thread_counting, int, uint32_t)                  \
    KPERF_FUNC(kperf_sample_get, int, int *)

/**
 * ====================
 *    Implementation
 * ====================
 */
struct PerfCounter {
    explicit PerfCounter() {
        auto kperf = load_kperf();
        hook_kperf_symbols(kperf);
    }

protected:
// Define kperf functions for later access
#define KPERF_FUNC(func_sym, return_value, ...)        \
    typedef return_value func_sym##_type(__VA_ARGS__); \
    func_sym##_type *func_sym;
    KPERF_FUNCTIONS_LIST
#undef KPERF_FUNC

private:
    forceinline void hook_kperf_symbols(void *kperf) {
#define KPERF_FUNC(func_sym, return_value, ...)                                                           \
    func_sym = (func_sym##_type *) (dlsym(kperf, #func_sym));                                             \
    if (!func_sym) {                                                                                      \
        throw std::runtime_error(std::string("kperf missing symbol: " #func_sym ": ").append(dlerror())); \
        return;                                                                                           \
    }
        KPERF_FUNCTIONS_LIST
#undef KPERF_FUNC
    };

    static forceinline void *load_kperf() {
        // Yoldlo: you only load dynamic libraries once
        static void *kperf = nullptr;
        if (kperf) return kperf;

        // Load kperf code into adress space
        kperf = dlopen(KPERF_FRAMEWORK_PATH, RTLD_LAZY);
        if (!kperf) {
            throw std::runtime_error(std::string("Unable to load kperf: ").append(dlerror()));
        }

        return kperf;
    };
};

#undef forceinline
#undef KPERF_FRAMEWORK_PATH
#undef KPERF_FUNCTIONS_LIST

#endif
