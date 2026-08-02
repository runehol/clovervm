#include "cpp_benchmarks.h"

#include <stdexcept>

namespace benchmark_cpp
{
    int64_t exception_bare_handler_raise_run(int64_t n)
    {
        int64_t acc = 0;
        int64_t counter = 0;
        while(counter < n)
        {
            try
            {
                throw std::runtime_error("benchmark exception");
            }
            catch(...)
            {
                acc += counter;
            }
            preserve_benchmark_loop_value(acc);
            counter += 1;
        }
        return acc;
    }

    int64_t exception_bare_handler_raise_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
