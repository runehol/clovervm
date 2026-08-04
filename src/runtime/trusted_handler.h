#ifndef CL_TRUSTED_HANDLER_H
#define CL_TRUSTED_HANDLER_H

#include "object_model/value.h"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cl
{
    class ThreadState;
    class VirtualMachine;

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

    struct TrustedHandlerMetadata
    {
        TrustedHandlerArity arity;
        TrustedHandlerEffects effects;
        TrustedHandlerSemantics semantics;

        bool operator==(const TrustedHandlerMetadata &) const = default;
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

    enum class TrustedResolutionKind
    {
        NoTrustedHandlerCallUntrusted,
        TrustedHandler,
        KnownNotImplementedSkipMethod,
    };

    struct TrustedResolution
    {
        TrustedResolutionKind kind =
            TrustedResolutionKind::NoTrustedHandlerCallUntrusted;
        TrustedHandlerArity arity = TrustedHandlerArity::None;

        union
        {
            UnaryHandler unary;
            BinaryHandler binary;
            TernaryHandler ternary;
        };

        TrustedResolution() : unary(nullptr) {}

        static TrustedResolution no_trusted_handler_call_untrusted()
        {
            return TrustedResolution();
        }

        static TrustedResolution call_trusted(UnaryHandler handler)
        {
            TrustedResolution resolution;
            resolution.kind = TrustedResolutionKind::TrustedHandler;
            resolution.arity = TrustedHandlerArity::Unary;
            resolution.unary = handler;
            return resolution;
        }

        static TrustedResolution call_trusted(BinaryHandler handler)
        {
            TrustedResolution resolution;
            resolution.kind = TrustedResolutionKind::TrustedHandler;
            resolution.arity = TrustedHandlerArity::Binary;
            resolution.binary = handler;
            return resolution;
        }

        static TrustedResolution call_trusted(TernaryHandler handler)
        {
            TrustedResolution resolution;
            resolution.kind = TrustedResolutionKind::TrustedHandler;
            resolution.arity = TrustedHandlerArity::Ternary;
            resolution.ternary = handler;
            return resolution;
        }

        template <TrustedHandlerFunction Target>
        static TrustedResolution call_registered(Target handler)
        {
            return call_trusted(handler);
        }

        static TrustedResolution known_not_implemented_skip_method()
        {
            TrustedResolution resolution;
            resolution.kind =
                TrustedResolutionKind::KnownNotImplementedSkipMethod;
            return resolution;
        }

        bool has_trusted_handler() const
        {
            return kind == TrustedResolutionKind::TrustedHandler;
        }
    };

    template <auto Target, TrustedHandlerEffects Effects,
              TrustedHandlerSemantics Semantics =
                  TrustedHandlerSemantics::Generic>
    class TrustedHandlerDefinition
    {
        static_assert(is_trusted_handler_function_v<decltype(Target)>);
        static_assert(Target != nullptr);

    public:
        static constexpr auto target = Target;
        static constexpr TrustedHandlerArity arity =
            trusted_handler_arity(Target);
        static constexpr TrustedHandlerEffects effects = Effects;
        static constexpr TrustedHandlerSemantics semantics = Semantics;

        static void register_with(VirtualMachine &vm);

        static TrustedResolution resolution()
        {
            return TrustedResolution::call_registered(Target);
        }
    };

    template <typename... HandlerDefinitions> class TrustedHandlerResolverBase
    {
    protected:
        template <size_t Index>
        using Handler =
            std::tuple_element_t<Index, std::tuple<HandlerDefinitions...>>;

    public:
        static void register_handlers(VirtualMachine &vm)
        {
            (HandlerDefinitions::register_with(vm), ...);
        }
    };

}  // namespace cl

#endif  // CL_TRUSTED_HANDLER_H
