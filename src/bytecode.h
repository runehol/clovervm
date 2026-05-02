#ifndef CL_BYTECODE_H
#define CL_BYTECODE_H

#include <cassert>
#include <cstdint>
#include <cstdlib>

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

        // load accumulator from register
        Ldar,
        // load accumulator from local binding register and raise if absent
        LoadLocalChecked,
        // clear local binding register to absent
        ClearLocal,
        // store accumulator to register
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

        // load and store globals by slot index
        LdaGlobal,
        StaGlobal,
        DelGlobal,
        DelLocal,

        // attribute access
        LoadAttr,
        StoreAttr,
        DelAttr,
        LoadSubscript,
        StoreSubscript,
        DelSubscript,
        CallMethodAttr,

        // binary on accumulator. reg op acc
        Add,
        Sub,
        Mul,
        Div,
        IntDiv,
        Pow,
        LeftShift,
        RightShift,  // arithmetic shift
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

        // binary on smi. acc op smi
        AddSmi,
        SubSmi,
        MulSmi,
        DivSmi,
        IntDivSmi,
        PowSmi,
        LeftShiftSmi,
        RightShiftSmi,  // arithmetic shift
        ModSmi,
        BitwiseOrSmi,
        BitwiseAndSmi,
        BitwiseXorSmi,

        Not,
        Negate,
        Plus,
        BitwiseNot,

        Nop,

        // function calls
        CallSimple,
        CallNative0,
        CallNative1,
        CallNative2,
        CallNative3,
        EnterPreparedFunction,

        // iteration
        GetIter,
        ForIter,
        ForPrepRange1,
        ForPrepRange2,
        ForPrepRange3,
        ForIterRange1,
        ForIterRangeStep,

        // allocations
        CreateDict,
        CreateList,
        CreateTuple,
        CreateFunction,
        CreateFunctionWithDefaults,
        CreateClass,
        CreateInstanceKnownClass,
        BuildClass,
        CheckInitReturnedNone,
        Assert,

        // control flow
        Return,
        Halt,
        Jump,
        JumpIfTrue,
        JumpIfFalse,

        Invalid
    };

    static constexpr size_t BytecodeTableSize = size_t(Bytecode::Invalid) + 1;

}  // namespace cl

#endif  // CL_BYTECODE_H
