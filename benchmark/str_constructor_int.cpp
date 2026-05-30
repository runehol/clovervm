#include "cpp_benchmarks.h"

#include <string>

namespace benchmark_cpp
{
    int64_t str_constructor_int_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            std::string value = std::to_string(i);
            acc += static_cast<int64_t>(value.size());
        }
        return acc;
    }

    int64_t str_constructor_int_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
