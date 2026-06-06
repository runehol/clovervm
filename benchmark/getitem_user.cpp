#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct GetItemUserBag
        {
            int64_t a = 61;
            int64_t b = 67;
            int64_t c = 71;
            int64_t d = 73;

            int64_t getitem(int64_t key) const
            {
                if(key == 0)
                {
                    return a;
                }
                if(key == 1)
                {
                    return b;
                }
                if(key == 2)
                {
                    return c;
                }
                return d;
            }
        };
    }  // namespace

    int64_t getitem_user_run(int64_t n)
    {
        const GetItemUserBag values;
        int64_t acc = 0;
        int64_t idx = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += values.getitem(idx);
            ++idx;
            if(idx == 4)
            {
                idx = 0;
            }
        }
        return acc;
    }

    int64_t getitem_user_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
