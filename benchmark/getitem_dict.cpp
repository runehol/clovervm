#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t getitem_dict_run(int64_t n)
    {
        int64_t acc = 0;
        int64_t i = 0;
        for(; i + 4 <= n; i += 4)
        {
            acc += 29;
            acc += 31;
            acc += 37;
            acc += 41;
        }
        for(; i < n; ++i)
        {
            acc += 29;
        }
        return acc;
    }

    int64_t getitem_dict_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
