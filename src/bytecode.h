#ifndef CL_BYTECODE_H
#define CL_BYTECODE_H

#include <cassert>

namespace cl
{

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


        // binary on accumulator. reg op acc
        Add,
        Sub,
        Mul,
        Div,
        IntDiv,
        Pow,
        LeftShift,
        RightShift,
        Mod,
        BitwiseOr,
        BitwiseAnd,
        BitwiseXor,

        //binary on smi. acc op smi
        AddSmi,
        SubSmi,
        MulSmi,
        DivSmi,
        IntDivSmi,
        PowSmi,
        LeftShiftSmi,
        RightShiftSmi,
        ModSmi,
        BitwiseOrSmi,
        BitwiseAndSmi,
        BitwiseXorSmi,

        Not,
        Negate,
        Plus,
        BitwiseNot,


        Return,

        Invalid
    };


}


#endif //CL_BYTECODE_H
