#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Counter
        {
            int64_t value = 0;
        };
    }  // namespace

    int64_t instance_attribute_write_run(int64_t n)
    {
        Counter obj;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            obj.value = i;
            preserve_benchmark_loop_value(obj);
            acc += obj.value;
        }
        return acc;
    }

    int64_t instance_attribute_write_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
