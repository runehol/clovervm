#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Empty
        {
        };
    }  // namespace

    int64_t class_instantiation_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            Empty obj;
            (void)obj;
            acc += 1;
        }
        return acc;
    }

    int64_t class_instantiation_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
