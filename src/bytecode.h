#ifndef CL_BYTECODE_H
#define CL_BYTECODE_H

#include <cassert>

namespace cl
{

    static int32_t n_fastpath_ldar_star = 16;

    enum class Bytecode : uint8_t
    {
        // load value into accumulator from various constants
        LdaConstant,
        LdaSmi,
        LdaTrue,
        LdaFalse,
        LdaNone,

        //load accumulator from register
        Ldar,
        //store accumulator to register
        Star,

        // fast single-byte optimisation opcodes for the first 16 registers
        Ldar0,
        Ldar1,
        Ldar2,
        Ldar3,
        Ldar4,
        Ldar5,
        Ldar6,
        Ldar7,
        Ldar8,
        Ldar9,
        Ldar10,
        Ldar11,
        Ldar12,
        Ldar13,
        Ldar14,
        Ldar15,

        Star0,
        Star1,
        Star2,
        Star3,
        Star4,
        Star5,
        Star6,
        Star7,
        Star8,
        Star9,
        Star10,
        Star11,
        Star12,
        Star13,
        Star14,
        Star15,

        //load and store globals by slot index
        LdaGlobal,
        StaGlobal,

        // binary on accumulator. reg op acc
        Add,
        Sub,
        Mul,
        Div,
        IntDiv,
        Pow,
        LeftShift,
        RightShift, //arithmetic shift
        Mod,
        BitwiseOr,
        BitwiseAnd,
        BitwiseXor,

        TestEqual,
        TestNotEqual,
        TestLess,
        TestLessEqual,
        TestGreater,
        TestGreaterEqual,
        TestIs,
        TestIsNot,
        TestIn,
        TestNotIn,


        //binary on smi. acc op smi
        AddSmi,
        SubSmi,
        MulSmi,
        DivSmi,
        IntDivSmi,
        PowSmi,
        LeftShiftSmi,
        RightShiftSmi, //arithmetic shift
        ModSmi,
        BitwiseOrSmi,
        BitwiseAndSmi,
        BitwiseXorSmi,

        Not,
        Negate,
        Plus,
        BitwiseNot,

        Nop,

        //function calls
        CallSimple,


        // allocations
        CreateFunction,

        // control flow
        Return,
        Halt,
        Jump,
        JumpIfTrue,
        JumpIfFalse,


        Invalid
    };

    static constexpr size_t BytecodeTableSize = size_t(Bytecode::Invalid)+1;


}


#endif //CL_BYTECODE_H
