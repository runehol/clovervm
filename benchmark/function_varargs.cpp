#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t add_varargs(int64_t first, int64_t second)
        {
            return first + second;
        }
    }  // namespace

    int64_t function_varargs_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += add_varargs(i, 1);
        }
        return acc;
    }

    int64_t function_varargs_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
