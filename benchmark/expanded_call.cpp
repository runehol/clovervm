#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t expanded_call_three_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += 2 * 3 + 5;
            preserve_benchmark_loop_value(acc);
        }
        return acc;
    }

    int64_t expanded_call_ten_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10;
            preserve_benchmark_loop_value(acc);
        }
        return acc;
    }

    int64_t expanded_call_mixed_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += 2 * 3 + 5 + 7;
            preserve_benchmark_loop_value(acc);
        }
        return acc;
    }

    int64_t expanded_call_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
