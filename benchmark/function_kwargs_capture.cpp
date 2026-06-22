#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t count_kwargs(int64_t a, int64_t b, int64_t c)
        {
            (void)a;
            (void)b;
            (void)c;
            return 1;
        }
    }  // namespace

    int64_t function_kwargs_capture_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += count_kwargs(i, 3, 2);
        }
        return acc;
    }

    int64_t function_kwargs_capture_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
