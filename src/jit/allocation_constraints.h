#ifndef CL_JIT_ALLOCATION_CONSTRAINTS_H
#define CL_JIT_ALLOCATION_CONSTRAINTS_H

#include "jit/instruction.h"
#include "jit/physical_register.h"

#include <optional>
#include <utility>
#include <vector>

namespace cl::jit
{
    enum class AccessTiming : uint8_t
    {
        Early,
        Late,
    };

    class RegisterRequirement
    {
    public:
        enum class Kind : uint8_t
        {
            Any,
            Fixed,
            SameAsInput,
        };

        static RegisterRequirement any(RegisterClass register_class);
        static RegisterRequirement fixed(PhysicalRegister reg);
        static RegisterRequirement same_as_input(uint32_t operand_index);

        Kind kind() const { return kind_; }
        RegisterClass register_class() const;
        PhysicalRegister fixed_register() const;
        uint32_t input_index() const;

    private:
        RegisterRequirement(Kind kind, uint32_t payload)
            : kind_(kind), payload_(payload)
        {
        }

        Kind kind_;
        uint32_t payload_;
    };

    struct ProgramValueUseConstraint
    {
        ProgramValueUseConstraint(uint32_t operand_index, AccessTiming timing,
                                  RegisterRequirement requirement);

        uint32_t operand_index;
        AccessTiming timing;
        RegisterRequirement requirement;
    };

    struct ResultConstraint
    {
        AccessTiming timing;
        RegisterRequirement requirement;
    };

    struct TemporaryConstraint
    {
        explicit TemporaryConstraint(RegisterRequirement requirement);

        RegisterRequirement requirement;
    };

    class InstructionAllocationConstraints
    {
    public:
        InstructionAllocationConstraints(
            const Instruction *instruction,
            std::vector<ProgramValueUseConstraint> input_overrides = {},
            std::optional<ResultConstraint> result_override = std::nullopt,
            std::vector<TemporaryConstraint> temporaries = {},
            RegisterSet clobbers = {});

        void validate() const;

        const Instruction *instruction() const { return instruction_; }

        const std::vector<ProgramValueUseConstraint> &input_overrides() const
        {
            return input_overrides_;
        }

        const std::optional<ResultConstraint> &result_override() const
        {
            return result_override_;
        }

        const std::vector<TemporaryConstraint> &temporaries() const
        {
            return temporaries_;
        }

        const RegisterSet &clobbers() const { return clobbers_; }

    private:
        const Instruction *instruction_;
        std::vector<ProgramValueUseConstraint> input_overrides_;
        std::optional<ResultConstraint> result_override_;
        std::vector<TemporaryConstraint> temporaries_;
        RegisterSet clobbers_;
    };

    class AllocationConstraints
    {
    public:
        AllocationConstraints(
            std::vector<RegisterClassDefinition> register_classes,
            std::vector<InstructionAllocationConstraints>
                instruction_overrides);

        const std::vector<RegisterClassDefinition> &register_classes() const
        {
            return register_classes_;
        }

        const std::vector<InstructionAllocationConstraints> &
        instruction_overrides() const
        {
            return instruction_overrides_;
        }

    private:
        std::vector<RegisterClassDefinition> register_classes_;
        std::vector<InstructionAllocationConstraints> instruction_overrides_;
    };

    RegisterClass
    register_class_for_representation(ValueRepresentation representation);
    ProgramValueUseConstraint
    default_program_value_use_constraint(uint32_t operand_index,
                                         ValueRepresentation representation);
    ResultConstraint
    default_result_constraint(ValueRepresentation representation);
    constexpr AccessTiming default_snapshot_use_timing()
    {
        return AccessTiming::Late;
    }

}  // namespace cl::jit

#endif  // CL_JIT_ALLOCATION_CONSTRAINTS_H
