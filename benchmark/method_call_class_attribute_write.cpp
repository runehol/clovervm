#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Counter
        {
            int64_t bump(int64_t new_value) const
            {
                value = new_value;
                return value;
            }

            static int64_t value;
        };

        int64_t Counter::value = 0;
    }  // namespace

    int64_t method_call_class_attribute_write_run(int64_t n)
    {
        Counter obj;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            acc += obj.bump(i);
        }
        return acc;
    }

    int64_t method_call_class_attribute_write_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
