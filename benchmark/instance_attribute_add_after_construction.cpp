#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    struct PairAfterConstruction
    {
        int64_t left = 0;
        int64_t right = 0;
    };

    int64_t instance_attribute_add_after_construction_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            PairAfterConstruction obj;
            obj.left = i;
            obj.right = i + 1;
            acc += obj.left + obj.right;
        }
        return acc;
    }

    int64_t instance_attribute_add_after_construction_items(int64_t n)
    {
        return n;
    }
}  // namespace benchmark_cpp
