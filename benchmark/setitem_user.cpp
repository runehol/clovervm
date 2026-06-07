#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct SetItemUserBag
        {
            int64_t a = 0;
            int64_t b = 0;
            int64_t c = 0;
            int64_t d = 0;

            void setitem(int64_t key, int64_t value)
            {
                if(key == 0)
                {
                    a = value;
                    return;
                }
                if(key == 1)
                {
                    b = value;
                    return;
                }
                if(key == 2)
                {
                    c = value;
                    return;
                }
                d = value;
            }
        };
    }  // namespace

    int64_t setitem_user_run(int64_t n)
    {
        SetItemUserBag values;
        int64_t idx = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            values.setitem(idx, i);
            ++idx;
            if(idx == 4)
            {
                idx = 0;
            }
        }
        return values.a + values.b + values.c + values.d;
    }

    int64_t setitem_user_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
