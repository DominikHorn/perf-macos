# Perf MacOS

This is an effort to bring userland hardware perf-counters to MacOS as a single header c++ interface library. Hardware
perf counters are usually subject to tight permission restrictions, i.e., may only be executed on ring-0. Therefore,
Kernel level support is usually required to access hardware performance counters.

One known route for perf support is to write and load a KEXT which provides access to these instructions. Since this
might negatively affect system stability and is generally not userfriendly, this project does not rely on a KEXT.
Instead, it is based on the XNU perf counter implementation:

* [pmc.h](https://opensource.apple.com/source/xnu/xnu-2050.18.24/osfmk/pmc/pmc.h.auto.html).
* [arm64 kpc.c](https://opensource.apple.com/source/xnu/xnu-4570.1.46/osfmk/arm64/kpc.c.auto.html)
* [x86_64 kpc.c](https://opensource.apple.com/source/xnu/xnu-4570.1.46/osfmk/x86_64/kpc_x86.c.auto.html)
* ... (more files exist in XNU that this project depends on)

To be able to access these kernel features, perf-macos uses the private
Framework [kperf](http://newosxbook.com/src.jl?tree=xnu&file=/osfmk/kperf/kperf.h).

Note that relying on the private kperf framework and undocumented kernel APIs means that this code could break at any
point without further notice. Please also acknowledge that you can not submit Apps to the MacOS Appstore that contain
this code due to the use of private APIs. You were warned.

# Usage

For full usage examples, see test.cpp.

## Basic Usage

```c++
#include "perf-macos.hpp"

// ...

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
```

## BlockCounter

```c++
#include "perf-macos.hpp"

// ...

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
```

## Output

Note that the tested CPU only has 4 configurable perf counter registers and therefore only 4 concurrent measurements
were taken:

```
   Elapses [ns]   Instructions  Branch misses      L1 misses     LLC misses
       0.177785       2.250112       0.000010       0.000000       0.000000
```

# Alternatives

Consider using the official "Counters" instrument template from the Instruments App, which is currently delivered as
part of the [Xcode IDE](https://developer.apple.com/xcode/features/).

