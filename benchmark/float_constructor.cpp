#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t float_constructor_float_run(int64_t n)
    {
        double value = 12345.0;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            double converted = value;
            preserve_benchmark_loop_value(converted);
            if(converted == value)
                ++acc;
        }
        return acc;
    }
    int64_t float_constructor_float_items(int64_t n) { return n; }

    int64_t float_constructor_int_run(int64_t n)
    {
        int64_t value = 12345;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            double converted = static_cast<double>(value);
            preserve_benchmark_loop_value(converted);
            if(converted == value)
                ++acc;
        }
        return acc;
    }
    int64_t float_constructor_int_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
