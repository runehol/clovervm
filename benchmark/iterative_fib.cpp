#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t fibonacci_value(int64_t n)
        {
            int64_t a = 0;
            int64_t b = 1;
            int64_t i = 0;
            while(i != n)
            {
                int64_t next_value = a + b;
                a = b;
                b = next_value;
                preserve_benchmark_loop_value(b);
                ++i;
            }
            return a;
        }
    }  // namespace

    int64_t iterative_fib_run(int64_t n)
    {
        int64_t accumulator = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            accumulator ^= fibonacci_value(80);
            preserve_benchmark_loop_value(accumulator);
        }
        return accumulator;
    }

    int64_t iterative_fib_items(int64_t n) { return n * 80; }
}  // namespace benchmark_cpp
