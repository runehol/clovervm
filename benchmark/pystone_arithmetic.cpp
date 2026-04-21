#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct PystoneArithmeticContext
        {
            int64_t tag = 0;
            int64_t accum = 0;
        };

        int64_t pystone_arithmetic_proc7(int64_t a, int64_t b)
        {
            return a + b + 2;
        }

        int64_t pystone_arithmetic_proc2(int64_t seed, int64_t limit)
        {
            int64_t total = 0;
            int64_t i = 0;
            while(i < limit)
            {
                total += pystone_arithmetic_proc7(seed, i);
                if(total > 1000)
                {
                    total -= 333;
                }
                else
                {
                    total += 17;
                }
                ++i;
            }
            return total;
        }

        int64_t pystone_arithmetic_proc6(int64_t tag)
        {
            if(tag == 1)
            {
                return 2;
            }
            if(tag == 2)
            {
                return 3;
            }
            if(tag == 3)
            {
                return 1;
            }
            return 1;
        }

        int64_t pystone_arithmetic_proc1(PystoneArithmeticContext &ctx,
                                         int64_t seed)
        {
            int64_t acc = seed;
            int64_t outer = 0;
            while(outer < 5)
            {
                acc += pystone_arithmetic_proc2(acc + outer, 6);
                if(acc > 5000)
                {
                    acc -= 777;
                }
                else
                {
                    acc += 111;
                }
                ++outer;
            }
            ctx.tag = pystone_arithmetic_proc6(ctx.tag);
            ctx.accum += acc;
            return acc + ctx.tag;
        }
    }  // namespace

    int64_t pystone_arithmetic_run(int64_t n)
    {
        PystoneArithmeticContext ctx{1, 0};

        int64_t total = 0;
        int64_t i = 0;
        int64_t seed = 3;
        while(i < n)
        {
            total += pystone_arithmetic_proc1(ctx, seed);
            total += pystone_arithmetic_proc2(seed, 4);
            if(total > 20000)
            {
                total -= 5000;
            }
            else
            {
                total += ctx.tag;
            }

            ++seed;
            if(seed > 8)
            {
                seed = 3;
            }
            ++i;
        }

        return total + ctx.accum + ctx.tag;
    }

    int64_t pystone_arithmetic_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
