#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t while_loop_run(int64_t n)
    {
        int64_t acc = 0;
        int64_t counter = 0;
        while(counter < n)
        {
            acc += counter;
            counter += 1;
        }
        return acc;
    }

    int64_t while_loop_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
