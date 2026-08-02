#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t memory_reclamation_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += i + (i + 15);
            preserve_benchmark_loop_value(acc);
        }
        return acc;
    }

    int64_t memory_reclamation_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
