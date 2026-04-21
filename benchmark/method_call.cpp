#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Incrementer
        {
            int64_t bump(int64_t value) const { return value + 1; }
        };
    }  // namespace

    int64_t method_call_run(int64_t n)
    {
        Incrementer obj;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += obj.bump(i);
        }
        return acc;
    }

    int64_t method_call_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
