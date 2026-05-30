#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t int_constructor_int_run(int64_t n)
    {
        int64_t value = 12345;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            int64_t converted = value;
            acc += converted;
        }
        return acc;
    }

    int64_t int_constructor_int_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
