#ifndef CL_OPERATOR_DISPATCH_H
#define CL_OPERATOR_DISPATCH_H

#include <cassert>
#include <cstdint>

namespace cl
{
    class String;

    enum class OperatorStepAction : uint8_t
    {
        CallBinary = 0,
        CallBinaryReflected = 1,
        IdentityEq = 2,
        IdentityNe = 4,
        RaiseUnsupported = 6,
        RaiseOrdering = 8,
        CallUnary = 10,
        CallTernary = 12,
        CallTernaryReflected = 13,
        CallIterMembershipFallback = 14,
        CallSequenceMembershipFallback = 16,
        TruthDefaultTrue = 18,
    };

    static constexpr bool
    operator_step_action_is_reflected(OperatorStepAction action)
    {
        return (static_cast<uint8_t>(action) & 1) != 0;
    }

    static_assert(
        !operator_step_action_is_reflected(OperatorStepAction::CallBinary));
    static_assert(operator_step_action_is_reflected(
        OperatorStepAction::CallBinaryReflected));
    static_assert(
        !operator_step_action_is_reflected(OperatorStepAction::IdentityEq));
    static_assert(
        !operator_step_action_is_reflected(OperatorStepAction::IdentityNe));
    static_assert(!operator_step_action_is_reflected(
        OperatorStepAction::RaiseUnsupported));
    static_assert(
        !operator_step_action_is_reflected(OperatorStepAction::RaiseOrdering));
    static_assert(
        !operator_step_action_is_reflected(OperatorStepAction::CallUnary));
    static_assert(
        !operator_step_action_is_reflected(OperatorStepAction::CallTernary));
    static_assert(operator_step_action_is_reflected(
        OperatorStepAction::CallTernaryReflected));
    static_assert(!operator_step_action_is_reflected(
        OperatorStepAction::CallIterMembershipFallback));
    static_assert(!operator_step_action_is_reflected(
        OperatorStepAction::CallSequenceMembershipFallback));

    static_assert(!operator_step_action_is_reflected(
        OperatorStepAction::TruthDefaultTrue));

    enum class TruthResultKind : uint8_t
    {
        Bool,
        Length,
    };

    enum class OperatorStepApplicability : uint8_t
    {
        Always,
        IfMethodFound,
        IfMethodFoundAndOperands01TypesDiffer,
        IfArithmeticReflectedPriority,
        IfRichComparisonReflectedPriority,
    };

    enum class OperatorCacheability
    {
        Uncacheable,
        CacheableDirectOnly,
        CacheableMaybeReflected,
    };

    enum class OperatorDispatchTableId : uint32_t
    {
        CompareEq,
        CompareNe,
        CompareLt,
        CompareLe,
        CompareGt,
        CompareGe,
        Add,
        InPlaceAdd,
        Sub,
        InPlaceSub,
        Mul,
        InPlaceMul,
        MatMul,
        InPlaceMatMul,
        TrueDiv,
        InPlaceTrueDiv,
        FloorDiv,
        InPlaceFloorDiv,
        BinaryPow,
        InPlaceBinaryPow,
        TernaryPow,
        Mod,
        InPlaceMod,
        LShift,
        InPlaceLShift,
        RShift,
        InPlaceRShift,
        And,
        InPlaceAnd,
        Xor,
        InPlaceXor,
        Or,
        InPlaceOr,
        Neg,
        Pos,
        Invert,
        GetItem,
        SetItem,
        DelItem,
        Contains,
        Truthiness,
        Count,
    };

    static_assert(uint32_t(OperatorDispatchTableId::InPlaceAdd) ==
                  uint32_t(OperatorDispatchTableId::Add) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceSub) ==
                  uint32_t(OperatorDispatchTableId::Sub) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceMul) ==
                  uint32_t(OperatorDispatchTableId::Mul) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceMatMul) ==
                  uint32_t(OperatorDispatchTableId::MatMul) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceTrueDiv) ==
                  uint32_t(OperatorDispatchTableId::TrueDiv) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceFloorDiv) ==
                  uint32_t(OperatorDispatchTableId::FloorDiv) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceBinaryPow) ==
                  uint32_t(OperatorDispatchTableId::BinaryPow) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceLShift) ==
                  uint32_t(OperatorDispatchTableId::LShift) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceRShift) ==
                  uint32_t(OperatorDispatchTableId::RShift) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceMod) ==
                  uint32_t(OperatorDispatchTableId::Mod) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceOr) ==
                  uint32_t(OperatorDispatchTableId::Or) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceAnd) ==
                  uint32_t(OperatorDispatchTableId::And) + 1);
    static_assert(uint32_t(OperatorDispatchTableId::InPlaceXor) ==
                  uint32_t(OperatorDispatchTableId::Xor) + 1);

    struct OperatorStep
    {
        String *dunder_name = nullptr;
        OperatorStepAction action = OperatorStepAction::IdentityEq;
        OperatorStepApplicability applicability =
            OperatorStepApplicability::Always;
        uint8_t else_skip = 0;
        TruthResultKind truth_result_kind = TruthResultKind::Bool;

        static constexpr OperatorStep
        call_binary(String *dunder_name,
                    OperatorStepApplicability applicability,
                    uint8_t else_skip = 0)
        {
            return OperatorStep{dunder_name, OperatorStepAction::CallBinary,
                                applicability, else_skip};
        }

        static constexpr OperatorStep
        call_unary(String *dunder_name, OperatorStepApplicability applicability,
                   uint8_t else_skip = 0,
                   TruthResultKind truth_result_kind = TruthResultKind::Bool)
        {
            return OperatorStep{dunder_name, OperatorStepAction::CallUnary,
                                applicability, else_skip, truth_result_kind};
        }

        static constexpr OperatorStep
        call_binary_reflected(String *dunder_name,
                              OperatorStepApplicability applicability,
                              uint8_t else_skip = 0)
        {
            return OperatorStep{dunder_name,
                                OperatorStepAction::CallBinaryReflected,
                                applicability, else_skip};
        }

        static constexpr OperatorStep
        call_ternary(String *dunder_name,
                     OperatorStepApplicability applicability,
                     uint8_t else_skip = 0)
        {
            return OperatorStep{dunder_name, OperatorStepAction::CallTernary,
                                applicability, else_skip};
        }

        static constexpr OperatorStep
        call_ternary_reflected(String *dunder_name,
                               OperatorStepApplicability applicability,
                               uint8_t else_skip = 0)
        {
            return OperatorStep{dunder_name,
                                OperatorStepAction::CallTernaryReflected,
                                applicability, else_skip};
        }

        static constexpr OperatorStep identity_eq()
        {
            return OperatorStep{nullptr, OperatorStepAction::IdentityEq,
                                OperatorStepApplicability::Always};
        }

        static constexpr OperatorStep identity_ne()
        {
            return OperatorStep{nullptr, OperatorStepAction::IdentityNe,
                                OperatorStepApplicability::Always};
        }

        static constexpr OperatorStep raise_ordering()
        {
            return OperatorStep{nullptr, OperatorStepAction::RaiseOrdering,
                                OperatorStepApplicability::Always};
        }

        static constexpr OperatorStep raise_unsupported()
        {
            return OperatorStep{nullptr, OperatorStepAction::RaiseUnsupported,
                                OperatorStepApplicability::Always};
        }

        static constexpr OperatorStep
        call_iter_membership_fallback(String *dunder_name)
        {
            return OperatorStep{dunder_name,
                                OperatorStepAction::CallIterMembershipFallback,
                                OperatorStepApplicability::IfMethodFound};
        }

        static constexpr OperatorStep
        call_sequence_membership_fallback(String *dunder_name)
        {
            return OperatorStep{
                dunder_name, OperatorStepAction::CallSequenceMembershipFallback,
                OperatorStepApplicability::IfMethodFound};
        }
    };

    static_assert(sizeof(OperatorStep) == 16);

    struct OperatorDispatchTable
    {
        const OperatorStep *steps = nullptr;
        uint8_t n_steps = 0;

        const OperatorStep &step(uint8_t row) const
        {
            assert(row < n_steps);
            return steps[row];
        }
    };

}  // namespace cl

#endif
