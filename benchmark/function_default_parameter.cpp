#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t add_default(int64_t value, int64_t increment = 1)
        {
            return value + increment;
        }
    }  // namespace

    int64_t function_default_parameter_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += add_default(i);
        }
        return acc;
    }

    int64_t function_default_parameter_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
