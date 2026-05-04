#include "cpp_benchmarks.h"

#include <stdexcept>

namespace benchmark_cpp
{
    int64_t exception_typed_handler_no_raise_run(int64_t n)
    {
        int64_t acc = 0;
        int64_t counter = 0;
        while(counter < n)
        {
            try
            {
                acc += counter;
            }
            catch(const std::runtime_error &)
            {
                acc -= 1;
            }
            counter += 1;
        }
        return acc;
    }

    int64_t exception_typed_handler_no_raise_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
