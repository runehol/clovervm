#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Point
        {
            explicit Point(int64_t _value) : value(_value) {}

            int64_t value = 0;
        };
    }  // namespace

    int64_t class_instantiation_with_init_run(int64_t n)
    {
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            Point obj(i);
            (void)obj;
            acc += 1;
        }
        return acc;
    }

    int64_t class_instantiation_with_init_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
