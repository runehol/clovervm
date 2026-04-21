#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t for_loop_slow_path_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t x = 0; x < n; ++x)
        {
            acc += x;
        }
        return acc;
    }

    int64_t for_loop_slow_path_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
