#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        struct PystoneLiteRecord
        {
            PystoneLiteRecord *ptr_comp = nullptr;
            int64_t discr = 0;
            int64_t enum_comp = 0;
            int64_t int_comp = 0;
        };

        struct PystoneLiteContext
        {
            int64_t int_glob = 0;
            PystoneLiteRecord ptr_glb_next;
            PystoneLiteRecord ptr_glb;
            PystoneLiteRecord record_template;
        };

        void pystone_lite_copy_record(const PystoneLiteRecord &src,
                                      PystoneLiteRecord &dst)
        {
            dst.ptr_comp = src.ptr_comp;
            dst.discr = src.discr;
            dst.enum_comp = src.enum_comp;
            dst.int_comp = src.int_comp;
        }

        int64_t pystone_lite_proc6(int64_t enum_par_in)
        {
            if(enum_par_in == 1)
            {
                return 2;
            }
            if(enum_par_in == 2)
            {
                return 3;
            }
            if(enum_par_in == 3)
            {
                return 1;
            }
            return 1;
        }

        int64_t pystone_lite_proc7(int64_t int_par_i1, int64_t int_par_i2)
        {
            return int_par_i2 + int_par_i1 + 2;
        }

        PystoneLiteRecord *pystone_lite_proc3(PystoneLiteContext &ctx,
                                              PystoneLiteRecord *ptr_par_out)
        {
            if(ptr_par_out != nullptr)
            {
                ctx.int_glob = pystone_lite_proc7(10, ctx.int_glob);
                return ptr_par_out->ptr_comp;
            }

            return &ctx.ptr_glb;
        }

        int64_t pystone_lite_proc2(PystoneLiteContext &ctx, int64_t int_par_io)
        {
            int64_t int_loc = int_par_io + 10;
            while(int_loc > 3)
            {
                --int_loc;
                ctx.int_glob += int_loc;
            }
            return int_par_io + ctx.int_glob;
        }

        int64_t pystone_lite_proc1(PystoneLiteContext &ctx,
                                   PystoneLiteRecord &ptr_par_in)
        {
            PystoneLiteRecord &next_record = *ptr_par_in.ptr_comp;
            pystone_lite_copy_record(ctx.record_template, next_record);
            ptr_par_in.int_comp = 5;
            next_record.int_comp = ptr_par_in.int_comp;
            next_record.ptr_comp = ptr_par_in.ptr_comp;
            next_record.ptr_comp =
                pystone_lite_proc3(ctx, next_record.ptr_comp);
            if(next_record.discr == 1)
            {
                next_record.int_comp = 6;
                next_record.enum_comp =
                    pystone_lite_proc6(ptr_par_in.enum_comp);
                next_record.int_comp =
                    pystone_lite_proc7(next_record.int_comp, 10);
            }
            else
            {
                pystone_lite_copy_record(next_record, ptr_par_in);
            }
            return ptr_par_in.int_comp + next_record.int_comp +
                   next_record.enum_comp;
        }
    }  // namespace

    int64_t pystone_lite_run(int64_t n)
    {
        PystoneLiteContext ctx;
        ctx.ptr_glb_next = {nullptr, 1, 3, 0};
        ctx.ptr_glb = {&ctx.ptr_glb_next, 1, 3, 40};
        ctx.record_template = {&ctx.ptr_glb, 1, 3, 0};

        int64_t total = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            total += pystone_lite_proc1(ctx, ctx.ptr_glb);
            total += pystone_lite_proc2(ctx, 2);
            total += pystone_lite_proc7(2, 3);

            ctx.ptr_glb.enum_comp = pystone_lite_proc6(ctx.ptr_glb.enum_comp);
            ctx.ptr_glb.int_comp += 1;
            if(ctx.ptr_glb.int_comp > 50)
            {
                ctx.ptr_glb.int_comp -= 7;
            }

            total += ctx.ptr_glb.int_comp;
            total += ctx.ptr_glb.ptr_comp->int_comp;
            total += ctx.int_glob;
        }

        return total + ctx.ptr_glb.enum_comp + ctx.ptr_glb.int_comp +
               ctx.ptr_glb.ptr_comp->int_comp;
    }

    int64_t pystone_lite_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
