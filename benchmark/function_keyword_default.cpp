#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t add_default(int64_t a, int64_t b = 1, int64_t c = 2)
        {
            return a * b + c;
        }
    }  // namespace

    int64_t function_keyword_default_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += add_default(i, 3);
        }
        return acc;
    }

    int64_t function_keyword_default_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
