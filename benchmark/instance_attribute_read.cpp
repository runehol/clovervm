#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Pair
        {
            int64_t left = 1;
            int64_t right = 2;
        };
    }  // namespace

    int64_t instance_attribute_read_run(int64_t n)
    {
        Pair obj;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += obj.left + obj.right;
        }
        return acc;
    }

    int64_t instance_attribute_read_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
