#ifndef CL_OPERATOR_DISPATCH_H
#define CL_OPERATOR_DISPATCH_H

#include <cassert>
#include <cstdint>

namespace cl
{
    class String;

    enum class OperatorStepAction : uint8_t
    {
        CallBinary,
        CallBinaryReflected,
        IdentityEq,
    };

    enum class OperatorStepApplicability : uint8_t
    {
        Always,
        IfMethodFound,
        IfRichComparisonReflectedPriority,
    };

    enum class OperatorDispatchTableId : uint32_t
    {
        CompareEq,
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
