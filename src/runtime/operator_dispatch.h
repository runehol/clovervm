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
        RaiseOrdering = 6,
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
    static_assert(
        !operator_step_action_is_reflected(OperatorStepAction::RaiseOrdering));

    enum class OperatorStepApplicability : uint8_t
    {
        Always,
        IfMethodFound,
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
        Count,
    };

    struct OperatorStep
    {
        String *dunder_name = nullptr;
        OperatorStepAction action = OperatorStepAction::IdentityEq;
        OperatorStepApplicability applicability =
            OperatorStepApplicability::Always;
        uint8_t else_skip = 0;

        static constexpr OperatorStep
        call_binary(String *dunder_name,
                    OperatorStepApplicability applicability,
                    uint8_t else_skip = 0)
        {
            return OperatorStep{dunder_name, OperatorStepAction::CallBinary,
                                applicability, else_skip};
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
