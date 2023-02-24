#include "interpreter.h"

#include "cl_value.h"
#include "code_object.h"
#include "stack_frame.h"

namespace cl
{

#define NOINLINE __attribute__((noinline))
#define MUSTTAIL __attribute__((musttail))
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define PARAMS StackFrame *frame, const uint8_t *pc, CLValue accumulator, void *dispatch, const CodeObject *code_object
#define ARGS frame, pc, accumulator, dispatch, code_object

    using DispatchTableEntry = CLValue (*)(PARAMS);

    struct DispatchTable
    {
        DispatchTableEntry table[BytecodeTableSize];
    };



#define START(len) static constexpr uint32_t instr_len = len; auto *dispatch_fun = reinterpret_cast<DispatchTable *>(dispatch)->table[pc[instr_len]]
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




#define A_NOT_SMI() ((a.v & cl_tag_mask) != 0)
#define A_OR_B_NOT_SMI() (((a.v | b.v) & cl_tag_mask) != 0)





    NOINLINE CLValue raise_generic_exception(PARAMS)
    {
        throw std::runtime_error("Clovervm exception");
    }

    NOINLINE CLValue raise_unknown_opcode_exception(PARAMS)
    {
        throw std::runtime_error("Unknown opcode");
    }

    NOINLINE CLValue slow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }
    NOINLINE CLValue overflow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }

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
        if(unlikely(A_NOT_SMI()))
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
        if(unlikely(A_OR_B_NOT_SMI()))
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
        if(unlikely(A_NOT_SMI()))
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
        if(unlikely(A_OR_B_NOT_SMI()))
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
        if(unlikely(A_OR_B_NOT_SMI()))
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
        if(unlikely(A_NOT_SMI()))
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

    CLValue op_negate(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely(A_NOT_SMI()))
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

    CLValue op_return(PARAMS)
    {
        START(1);

        /* temporary implementation: return the accumulator value out of the interpreter */
        return accumulator;

        COMPLETE();
    }

    constexpr DispatchTable make_dispatch_table()
    {
        DispatchTable tbl;
        for(size_t i = 0; i < BytecodeTableSize; ++i)
        {
            tbl.table[i] = raise_unknown_opcode_exception;
        }
#define SET_TABLE_ENTRY(bytecode, fun) tbl.table[size_t(bytecode)] = fun
        SET_TABLE_ENTRY(Bytecode::Ldar, op_ldar);
        SET_TABLE_ENTRY(Bytecode::Star, op_star);
        SET_TABLE_ENTRY(Bytecode::Add, op_add);
        SET_TABLE_ENTRY(Bytecode::AddSmi, op_add_smi);
        SET_TABLE_ENTRY(Bytecode::Sub, op_sub);
        SET_TABLE_ENTRY(Bytecode::SubSmi, op_sub_smi);
        SET_TABLE_ENTRY(Bytecode::Mul, op_mul);
        SET_TABLE_ENTRY(Bytecode::MulSmi, op_mul_smi);
        SET_TABLE_ENTRY(Bytecode::Negate, op_negate);
        SET_TABLE_ENTRY(Bytecode::Return, op_return);
        return tbl;
    }


    DispatchTable dispatch_table = make_dispatch_table();


    CLValue run_interpreter(const CodeObject *code_object, uint32_t start_pc)
    {
        const uint8_t *pc = &code_object->code[start_pc];
        void *dispatch = reinterpret_cast<void *>(&dispatch_table);
        CLValue accumulator = value_make_smi(0); // init accumulator to 0

        StackFrame stack_frame; //temporarily make a stack frame here

        StackFrame *frame = &stack_frame;



        // do the initial dispatch
        auto *dispatch_fun = dispatch_table.table[*pc];
        return dispatch_fun(ARGS);


    }


}
