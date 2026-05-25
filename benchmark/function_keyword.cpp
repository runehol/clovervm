#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t add3(int64_t a, int64_t b, int64_t c) { return a * b + c; }
    }  // namespace

    int64_t function_keyword_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += add3(i, 3, 2);
        }
        return acc;
    }

    int64_t function_keyword_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
