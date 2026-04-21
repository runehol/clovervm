#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t fibonacci_value(int64_t n)
        {
            if(n <= 1)
            {
                return n;
            }
            return fibonacci_value(n - 1) + fibonacci_value(n - 2);
        }
    }  // namespace

    int64_t recursive_fib_run(int64_t n) { return fibonacci_value(n); }

    int64_t recursive_fib_items(int64_t n)
    {
        if(n <= 1)
        {
            return 1;
        }

        int64_t prev2 = 1;
        int64_t prev1 = 1;
        for(int64_t i = 2; i <= n; ++i)
        {
            int64_t current = 1 + prev1 + prev2;
            prev2 = prev1;
            prev1 = current;
        }
        return prev1;
    }
}  // namespace benchmark_cpp
