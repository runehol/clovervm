#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Box
        {
        };

        Box *volatile global_value = nullptr;
    }  // namespace

    int64_t global_refcounted_write_run(int64_t n)
    {
        Box a;
        Box b;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            global_value = &a;
            global_value = &b;
            acc += i;
        }
        return acc;
    }

    int64_t global_refcounted_write_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
