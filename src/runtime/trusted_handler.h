#ifndef CL_TRUSTED_HANDLER_H
#define CL_TRUSTED_HANDLER_H

#include "object_model/value.h"

#include <cstdint>

namespace cl
{
    class ThreadState;

    using UnaryHandler = Value (*)(ThreadState *, Value);
    using BinaryHandler = Value (*)(ThreadState *, Value, Value);
    using TernaryHandler = Value (*)(ThreadState *, Value, Value, Value);
    using TrustedHandlerTarget = void (*)();

    enum class TrustedHandlerArity : uint8_t
    {
        None,
        Unary,
        Binary,
        Ternary,
    };

    enum class TrustedHandlerEffects : uint8_t
    {
        None = 0,
        Allocate = 1 << 0,
        Safepoint = 1 << 1,
        Raise = 1 << 2,
        CallPython = 1 << 3,
    };

    constexpr TrustedHandlerEffects operator|(TrustedHandlerEffects lhs,
                                              TrustedHandlerEffects rhs)
    {
        return static_cast<TrustedHandlerEffects>(static_cast<uint8_t>(lhs) |
                                                  static_cast<uint8_t>(rhs));
    }

    constexpr bool has_trusted_handler_effects(TrustedHandlerEffects effects,
                                               TrustedHandlerEffects required)
    {
        uint8_t effect_bits = static_cast<uint8_t>(effects);
        uint8_t required_bits = static_cast<uint8_t>(required);
        return (effect_bits & required_bits) == required_bits;
    }

    enum class TrustedHandlerSemantics : uint16_t
    {
        Generic,
        Add,
        Sub,
        RSub,
        Mul,
        TrueDiv,
        RTrueDiv,
        FloorDiv,
        RFloorDiv,
        Mod,
        RMod,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        Neg,
        Pos,
    };

    template <typename Target> struct TrustedHandlerFunctionTraits
    {
        static constexpr bool supported = false;
    };

    template <> struct TrustedHandlerFunctionTraits<UnaryHandler>
    {
        static constexpr bool supported = true;
        static constexpr TrustedHandlerArity arity = TrustedHandlerArity::Unary;
    };

    template <> struct TrustedHandlerFunctionTraits<BinaryHandler>
    {
        static constexpr bool supported = true;
        static constexpr TrustedHandlerArity arity =
            TrustedHandlerArity::Binary;
    };

    template <> struct TrustedHandlerFunctionTraits<TernaryHandler>
    {
        static constexpr bool supported = true;
        static constexpr TrustedHandlerArity arity =
            TrustedHandlerArity::Ternary;
    };

    template <typename Target>
    inline constexpr bool is_trusted_handler_function_v =
        TrustedHandlerFunctionTraits<Target>::supported;

    template <typename Target>
    concept TrustedHandlerFunction = is_trusted_handler_function_v<Target>;

    template <TrustedHandlerFunction Target>
    constexpr TrustedHandlerArity trusted_handler_arity(Target)
    {
        return TrustedHandlerFunctionTraits<Target>::arity;
    }

    template <TrustedHandlerFunction Target>
    TrustedHandlerTarget erase_trusted_handler_target(Target target)
    {
        return reinterpret_cast<TrustedHandlerTarget>(target);
    }

}  // namespace cl

#endif  // CL_TRUSTED_HANDLER_H
