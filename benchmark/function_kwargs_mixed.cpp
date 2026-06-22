#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t add_kwargs_mixed(int64_t a, int64_t b, int64_t c, int64_t d,
                                 int64_t e)
        {
            return a * b + c + d + e;
        }
    }  // namespace

    int64_t function_kwargs_mixed_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += add_kwargs_mixed(i, 3, 2, 5, 7);
        }
        return acc;
    }

    int64_t function_kwargs_mixed_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
