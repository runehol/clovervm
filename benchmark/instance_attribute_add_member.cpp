#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    struct Pair
    {
        Pair(int64_t _left, int64_t _right) : left(_left), right(_right) {}

        int64_t left = 0;
        int64_t right = 0;
    };

    int64_t instance_attribute_add_member_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            Pair obj(i, i + 1);
            preserve_benchmark_loop_value(obj);
            acc += obj.left + obj.right;
        }
        return acc;
    }

    int64_t instance_attribute_add_member_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
