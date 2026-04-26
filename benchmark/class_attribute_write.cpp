#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Counter
        {
            static int64_t value;
        };

        int64_t Counter::value = 0;
    }  // namespace

    int64_t class_attribute_write_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            Counter::value = i;
            acc += Counter::value;
        }
        return acc;
    }

    int64_t class_attribute_write_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
