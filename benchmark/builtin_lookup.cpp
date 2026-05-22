#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    int64_t builtin_lookup_run(int64_t n) { return n * 3; }

    int64_t builtin_lookup_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
