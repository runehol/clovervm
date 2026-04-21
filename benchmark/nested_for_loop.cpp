#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        constexpr int64_t kInnerIterations = 10;
    }

    int64_t nested_for_loop_run(int64_t n)
    {
        int64_t total = 0;
        for(int64_t x = 0; x < n; ++x)
        {
            for(int64_t y = 0; y < kInnerIterations; ++y)
            {
                total += x * y;
            }
        }
        return total;
    }

    int64_t nested_for_loop_items(int64_t n) { return n * kInnerIterations; }
}  // namespace benchmark_cpp
