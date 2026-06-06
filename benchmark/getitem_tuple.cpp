#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t getitem_tuple_run(int64_t n)
    {
        const int64_t values[] = {13, 17, 19, 23};
        int64_t acc = 0;
        int64_t idx = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += values[idx];
            ++idx;
            if(idx == 4)
            {
                idx = 0;
            }
        }
        return acc;
    }

    int64_t getitem_tuple_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
