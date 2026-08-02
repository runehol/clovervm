#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t builtin_lookup_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += 3;
            preserve_benchmark_loop_value(acc);
        }
        return acc;
    }

    int64_t builtin_lookup_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
