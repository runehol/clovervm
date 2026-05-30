#include "cpp_benchmarks.h"

#include <cstdlib>

namespace benchmark_cpp
{
    int64_t int_constructor_string_run(int64_t n)
    {
        const char *value = "12345";
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += std::strtoll(value, nullptr, 10);
        }
        return acc;
    }

    int64_t int_constructor_string_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
