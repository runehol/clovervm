#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t getitem_str_run(int64_t n)
    {
        const char values[] = "abcd";
        int64_t acc = 0;
        int64_t idx = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            switch(values[idx])
            {
                case 'a':
                    acc += 43;
                    break;
                case 'b':
                    acc += 47;
                    break;
                case 'c':
                    acc += 53;
                    break;
                default:
                    acc += 59;
                    break;
            }
            ++idx;
            if(idx == 4)
            {
                idx = 0;
            }
        }
        return acc;
    }

    int64_t getitem_str_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
