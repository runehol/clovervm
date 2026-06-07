#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t setitem_dict_run(int64_t n)
    {
        int64_t values[] = {0, 0, 0, 0};
        int64_t i = 0;
        for(; i + 4 <= n; i += 4)
        {
            values[0] = i;
            values[1] = i + 1;
            values[2] = i + 2;
            values[3] = i + 3;
        }
        for(; i < n; ++i)
        {
            values[0] = i;
        }
        return values[0] + values[1] + values[2] + values[3];
    }

    int64_t setitem_dict_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
