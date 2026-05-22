#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        volatile int64_t global_value = 1;
    }  // namespace

    int64_t global_read_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += global_value;
        }
        return acc;
    }

    int64_t global_read_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
