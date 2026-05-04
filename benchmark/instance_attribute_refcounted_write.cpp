#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct Box
        {
        };

        struct Holder
        {
            Box *volatile value = nullptr;
        };
    }  // namespace

    int64_t instance_attribute_refcounted_write_run(int64_t n)
    {
        Holder obj;
        Box a;
        Box b;
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            obj.value = &a;
            obj.value = &b;
            acc += i;
        }
        return acc;
    }

    int64_t instance_attribute_refcounted_write_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
