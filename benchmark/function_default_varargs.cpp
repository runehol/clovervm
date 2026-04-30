#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t add_default_varargs(int64_t a, int64_t b = 10) { return a + b; }
    }  // namespace

    int64_t function_default_varargs_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += add_default_varargs(i);
        }
        return acc;
    }

    int64_t function_default_varargs_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
