#include "cl_value.h"
#include "code_object.h"
#include "stack_frame.h"

namespace cl
{

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)





#define PARAMS StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object
#define ARGS frame, pc, accumulator, dispatch, code_object

#define START(len) static constexpr uint32_t instr_len = len; auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[instr_len]]
#define COMPLETE() pc += instr_len; MUSTTAIL return dispatch_fun(ARGS)

#define START_BINARY_REG_ACC()               \
    START(2);                                \
    uint8_t reg = pc[1];                     \
    CLValue a = frame->registers[reg];       \
    CLValue b = accumulator

#define START_BINARY_ACC_SMI()               \
    START(2);                                \
    CLValue a = accumulator;                 \
    CLValue b = value_make_smi(int8_t(pc[1]))

#define START_UNARY_ACC()                    \
    START(1);                                \
    CLValue a = accumulator

    extern CLValue (*dispatch_table[])(PARAMS);

    using DispatchTable = decltype(&dispatch_table[0]);

#define MUSTTAIL __attribute__((musttail))


#define NOT_SMI_MASK 0x1f

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
    CLValue slow_path(PARAMS);
    CLValue overflow_path(PARAMS);

    CLValue op_ldar(PARAMS)
    {
        START(2);
        uint8_t reg = pc[1];
        accumulator = frame->registers[reg];
        COMPLETE();
    }

    CLValue op_star(PARAMS)
    {
        START(2);
        uint8_t reg = pc[1];
        frame->registers[reg] = accumulator;

        COMPLETE();
    }



    CLValue op_add_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely((a.v & NOT_SMI_MASK) != 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_saddll_overflow(a.v, b.v, &accumulator.v)))
        {
            accumulator.v -= b.v;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }


    CLValue op_add(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(((a.v | b.v) & NOT_SMI_MASK) != 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_saddll_overflow(a.v, b.v, &accumulator.v)))
        {
            accumulator.v -= a.v;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    CLValue op_sub_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely((a.v & NOT_SMI_MASK) != 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_ssubll_overflow(a.v, b.v, &accumulator.v)))
        {
            accumulator.v += b.v;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }






    CLValue op_sub(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(((a.v | b.v) & NOT_SMI_MASK) != 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_ssubll_overflow(a.v, b.v, &accumulator.v)))
        {
            accumulator.v += a.v;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    CLValue op_mul(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(((a.v | b.v) & NOT_SMI_MASK) != 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        CLValue dest;
        if(unlikely(__builtin_smulll_overflow(a.v, b.v>>5, &dest.v)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    CLValue op_mul_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely((a.v & NOT_SMI_MASK) != 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        CLValue dest;
        if(unlikely(__builtin_smulll_overflow(a.v, b.v>>5, &dest.v)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    CLValue op_neg(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely((a.v & NOT_SMI_MASK) != 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        if(unlikely(__builtin_ssubll_overflow(0, a.v, &accumulator.v)))
        {
            accumulator.v = -accumulator.v;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    CLValue (*dispatch_table[])(PARAMS) =
    {
        op_ldar,
        op_star,
        op_add,
        op_add_smi,
        op_sub,
        op_sub_smi,
        op_neg,
    };


}
