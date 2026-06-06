#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t setitem_list_run(int64_t n)
    {
        int64_t values[] = {0, 0, 0, 0};
        int64_t idx = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            values[idx] = i;
            ++idx;
            if(idx == 4)
            {
                idx = 0;
            }
        }
        return values[0] + values[1] + values[2] + values[3];
    }

    int64_t setitem_list_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
