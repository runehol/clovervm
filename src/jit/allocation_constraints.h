#ifndef CL_JIT_ALLOCATION_CONSTRAINTS_H
#define CL_JIT_ALLOCATION_CONSTRAINTS_H

#include "jit/instruction.h"
#include "jit/physical_location.h"
#include "runtime/fatal.h"

#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace cl::jit
{
    enum class AccessTiming : uint8_t
    {
        Early,
        Late,
    };

    class LocationRequirement
    {
    public:
        enum class Kind : uint8_t
        {
            AnyLocation,
            AnyRegister,
            FixedLocation,
            FixedOperandCopy,
            SameAsInput,
        };

        static LocationRequirement any_location()
        {
            return LocationRequirement(std::monostate{});
        }
        static LocationRequirement any_register(RegisterClass register_class)
        {
            return LocationRequirement(register_class);
        }
        static LocationRequirement fixed(PhysicalLocation location)
        {
            return LocationRequirement(location);
        }
        static LocationRequirement fixed_operand_copy(PhysicalRegister reg)
        {
            return LocationRequirement(reg);
        }
        static LocationRequirement same_as_input(uint32_t operand_index)
        {
            return LocationRequirement(operand_index);
        }

        Kind kind() const
        {
            switch(payload_.index())
            {
                case 0:
                    return Kind::AnyLocation;
                case 1:
                    return Kind::AnyRegister;
                case 2:
                    return Kind::FixedLocation;
                case 3:
                    return Kind::FixedOperandCopy;
                case 4:
                    return Kind::SameAsInput;
            }
            fatal("invalid JIT location requirement");
        }
        RegisterClass register_class() const
        {
            if(kind() != Kind::AnyRegister)
            {
                fatal("register_class() requires an AnyRegister requirement");
            }
            return std::get<RegisterClass>(payload_);
        }
        PhysicalLocation fixed_location() const
        {
            if(kind() != Kind::FixedLocation)
            {
                fatal("fixed_location() requires a FixedLocation requirement");
            }
            return std::get<PhysicalLocation>(payload_);
        }
        PhysicalRegister fixed_operand_copy_register() const
        {
            if(kind() != Kind::FixedOperandCopy)
            {
                fatal("fixed_operand_copy_register() requires a "
                      "FixedOperandCopy requirement");
            }
            return std::get<PhysicalRegister>(payload_);
        }
        uint32_t input_index() const
        {
            if(kind() != Kind::SameAsInput)
            {
                fatal("input_index() requires a SameAsInput location "
                      "requirement");
            }
            return std::get<uint32_t>(payload_);
        }

    private:
        explicit LocationRequirement(std::monostate payload) : payload_(payload)
        {
        }
        explicit LocationRequirement(RegisterClass register_class)
            : payload_(register_class)
        {
            if(register_class >= RegisterClass::Count)
            {
                fatal("invalid register class requirement");
            }
        }
        explicit LocationRequirement(PhysicalLocation location)
            : payload_(location)
        {
        }
        explicit LocationRequirement(PhysicalRegister reg) : payload_(reg) {}
        explicit LocationRequirement(uint32_t operand_index)
            : payload_(operand_index)
        {
        }

        std::variant<std::monostate, RegisterClass, PhysicalLocation,
                     PhysicalRegister, uint32_t>
            payload_;
    };

    struct ProgramValueUseConstraint
    {
        ProgramValueUseConstraint(uint32_t operand_index, AccessTiming timing,
                                  LocationRequirement requirement);

        uint32_t operand_index;
        AccessTiming timing;
        LocationRequirement requirement;
    };

    struct ResultConstraint
    {
        AccessTiming timing;
        LocationRequirement requirement;
    };

    struct TemporaryConstraint
    {
        explicit TemporaryConstraint(LocationRequirement requirement);

        LocationRequirement requirement;
    };

    class InstructionAllocationConstraints
    {
    public:
        InstructionAllocationConstraints(
            Instruction instruction,
            std::vector<ProgramValueUseConstraint> input_overrides = {},
            std::optional<ResultConstraint> result_override = std::nullopt,
            std::vector<TemporaryConstraint> temporaries = {},
            RegisterSet clobbers = {});

        void validate(const CompilationStorage &storage) const;

        InstructionId instruction_id() const { return instruction_; }

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
        InstructionId instruction_;
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
