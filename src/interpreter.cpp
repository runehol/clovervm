#include "interpreter.h"

#include "value.h"
#include "code_object.h"
#include "stack_frame.h"

namespace cl
{

#define NOINLINE __attribute__((noinline))
#define MUSTTAIL __attribute__((musttail))
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define PARAMS StackFrame *frame, const uint8_t *pc, Value accumulator, void *dispatch, const CodeObject *code_object
#define ARGS frame, pc, accumulator, dispatch, code_object

    using DispatchTableEntry = Value (*)(PARAMS);

    struct DispatchTable
    {
        DispatchTableEntry table[BytecodeTableSize];
    };



#define START(len) static constexpr uint32_t instr_len = len; auto *dispatch_fun = reinterpret_cast<DispatchTable *>(dispatch)->table[pc[instr_len]]
#define COMPLETE() pc += instr_len; MUSTTAIL return dispatch_fun(ARGS)

#define START_BINARY_REG_ACC()               \
    START(2);                                \
    uint8_t reg = pc[1];                     \
    Value a = frame->registers[reg];       \
    Value b = accumulator

#define START_BINARY_ACC_SMI()               \
    START(2);                                \
    Value a = accumulator;                 \
    Value b = Value::from_smi(int8_t(pc[1]))

#define START_UNARY_ACC()                    \
    START(1);                                \
    Value a = accumulator




#define A_NOT_SMI() ((a.as.integer & value_not_smi_mask) != 0)
#define A_OR_B_NOT_SMI() (((a.as.integer | b.as.integer) & value_not_smi_mask) != 0)





    NOINLINE Value raise_generic_exception(PARAMS)
    {
        throw std::runtime_error("Clovervm exception");
    }

    NOINLINE Value raise_unknown_opcode_exception(PARAMS)
    {
        throw std::runtime_error("Unknown opcode");
    }

    NOINLINE Value raise_value_error_negative_shift_count(PARAMS)
    {
        throw std::runtime_error("ValueError: negative shift count");
    }

    NOINLINE static Value slow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }
    NOINLINE static Value overflow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }

    static Value op_lda_constant(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        accumulator = code_object->constant_table[const_offset];
        COMPLETE();
    }

    static Value op_lda_smi(PARAMS)
    {
        START(2);
        int8_t smi = pc[1];
        accumulator = Value::from_smi(smi);
        COMPLETE();
    }

    static Value op_lda_true(PARAMS)
    {
        START(1);
        accumulator = cl_True;
        COMPLETE();
    }

    static Value op_lda_false(PARAMS)
    {
        START(1);
        accumulator = cl_False;
        COMPLETE();
    }

    static Value op_lda_none(PARAMS)
    {
        START(1);
        accumulator = cl_None;
        COMPLETE();
    }

    static Value op_ldar(PARAMS)
    {
        START(2);
        uint8_t reg = pc[1];
        accumulator = frame->registers[reg];
        COMPLETE();
    }

    static Value op_star(PARAMS)
    {
        START(2);
        uint8_t reg = pc[1];
        frame->registers[reg] = accumulator;

        COMPLETE();
    }



    static Value op_add_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_saddll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
        {
            accumulator.as.integer -= b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }


    static Value op_add(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_saddll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
        {
            accumulator.as.integer -= a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_sub_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_ssubll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
        {
            accumulator.as.integer += b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }






    static Value op_sub(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_ssubll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
        {
            accumulator.as.integer += a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_mul(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        Value dest;
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(), &dest.as.integer)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    static Value op_mul_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        Value dest;
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(), &dest.as.integer)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    static Value op_left_shift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer << shift_count;
        /* TODO need to test overflow here */

        COMPLETE();
    }

    static Value op_left_shift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer << shift_count;
        /* TODO need to test overflow here */

        COMPLETE();
    }



    static Value op_right_shift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;


        COMPLETE();
    }

    static Value op_right_shift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }





    static Value op_negate(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        if(unlikely(__builtin_ssubll_overflow(0, a.as.integer, &accumulator.as.integer)))
        {
            accumulator.as.integer = -accumulator.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_not(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely((a.as.integer & value_ptr_mask) != 0))
        {
            // this is not an inlined type, go to the slow path
            MUSTTAIL return slow_path(ARGS);
        }
        // however, if this is an inlined type, we can simply test for truthiness using a mask and negate

        if((a.as.integer & value_truthy_mask) != 0)
        {
            accumulator = cl_False;
        } else {
            accumulator = cl_True;
        }
        COMPLETE();


    }

    static Value op_return(PARAMS)
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
        SET_TABLE_ENTRY(Bytecode::LdaConstant, op_lda_constant);
        SET_TABLE_ENTRY(Bytecode::LdaSmi, op_lda_smi);
        SET_TABLE_ENTRY(Bytecode::LdaTrue, op_lda_true);
        SET_TABLE_ENTRY(Bytecode::LdaFalse, op_lda_false);
        SET_TABLE_ENTRY(Bytecode::LdaNone, op_lda_none);
        SET_TABLE_ENTRY(Bytecode::Add, op_add);
        SET_TABLE_ENTRY(Bytecode::AddSmi, op_add_smi);
        SET_TABLE_ENTRY(Bytecode::Sub, op_sub);
        SET_TABLE_ENTRY(Bytecode::SubSmi, op_sub_smi);
        SET_TABLE_ENTRY(Bytecode::Mul, op_mul);
        SET_TABLE_ENTRY(Bytecode::MulSmi, op_mul_smi);

        SET_TABLE_ENTRY(Bytecode::LeftShift, op_left_shift);
        SET_TABLE_ENTRY(Bytecode::LeftShiftSmi, op_left_shift_smi);
        SET_TABLE_ENTRY(Bytecode::RightShift, op_right_shift);
        SET_TABLE_ENTRY(Bytecode::RightShiftSmi, op_right_shift_smi);


        SET_TABLE_ENTRY(Bytecode::Negate, op_negate);
        SET_TABLE_ENTRY(Bytecode::Not, op_not);

        SET_TABLE_ENTRY(Bytecode::Return, op_return);
        return tbl;
    }


    DispatchTable dispatch_table = make_dispatch_table();


    Value run_interpreter(const CodeObject *code_object, uint32_t start_pc)
    {
        const uint8_t *pc = &code_object->code[start_pc];
        void *dispatch = reinterpret_cast<void *>(&dispatch_table);
        Value accumulator = Value::from_smi(0); // init accumulator to 0

        StackFrame stack_frame; //temporarily make a stack frame here

        StackFrame *frame = &stack_frame;



        // do the initial dispatch
        auto *dispatch_fun = dispatch_table.table[*pc];
        return dispatch_fun(ARGS);


    }


}
