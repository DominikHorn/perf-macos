# Perf MacOS

This is an effort to bring userland hardware perf-counters to MacOS as a single
header c++ interface library. Hardware perf counters are usually subject to
tight permission restrictions, i.e., may only be executed on ring-0. Therefore,
Kernel level support is usually required to access hardware performance
counters. 

The natural route for perf support might therefore be to write and load a KEXT,
which provides access to these instructions. Since this could potential affect
system stability and is generally not easy to use, this project focuses on a
different route. As it turns out, XNU has pmc support, partially implemented in
the following files:

* [pmc.h](https://opensource.apple.com/source/xnu/xnu-2050.18.24/osfmk/pmc/pmc.h.auto.html).
* [arm64 kpc.c](https://opensource.apple.com/source/xnu/xnu-4570.1.46/osfmk/arm64/kpc.c.auto.html)
* [x86_64 kpc.c](https://opensource.apple.com/source/xnu/xnu-4570.1.46/osfmk/x86_64/kpc_x86.c.auto.html)

As well as a private framework API, known as [kperf](http://newosxbook.com/src.jl?tree=xnu&file=/osfmk/kperf/kperf.h)

Note that relying on the private kperf framework as well as, to the best of my
knowlege, unofficial kernel APIs means that this code could break at any point
with any update without further notice. Please also acknowledge that you can not
submit Apps to the MacOS Appstore that contain this code due to the use of
private APIs.

# Usage

Simply allocate a PerfCounter in the scope you want to benchmark. Measurements
will be taken from the point of allocation up to PerfCounter's destruction,
i.e., when it goes out of scope:

```c++
#include "perf-macos.hpp"

#include <cstdint>
#include <iostream>

int main() {
    const uint64_t test_rep_cnt = 1000000;
    uint64_t acc = 0x0;

    {
        // This will automatically kickoff measurements and stop as soon as ctr
        // is destructed
        const auto ctr = PerfCounter(test_rep_cnt);

        // Code to benchmark
        for (uint64_t i = 0; i < test_rep_cnt; i++) { acc ^= i * 0xABCDEF010; }
    }

    // Lay mans hack to prevent compiler from removing the benchmark code
    std::cout << acc << std::endl;

    return 0;
}
```

Output. Note that the tested CPU only has 4 configurable perf counter registers
and therefore only 4 concurrent measurements are possible:
```
averages after 1000000 repetitions: 
   instructions      L1 misses     LLC misses  branch misses         cycles       branches
       2.256831       0.000000       0.000208       0.000130              -              -
```

# Alternatives

Consider using the official "Counters" instrument template from the Instruments App, 
which is currently delivered as part of the [Xcode IDE](https://developer.apple.com/xcode/features/).

