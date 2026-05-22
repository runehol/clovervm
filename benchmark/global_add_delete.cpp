#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        volatile int64_t global_value = 0;
    }  // namespace

    int64_t global_add_delete_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            global_value = i;
            acc += global_value;
            global_value = 0;
        }
        return acc;
    }

    int64_t global_add_delete_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
