#include "jit/allocation_constraints.h"

#include "runtime/fatal.h"

#include <bit>
#include <cstddef>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        static_assert(
            std::has_single_bit(PhysicalRegister::MaxRegistersPerClass));
        constexpr unsigned RegisterNumberBits =
            std::bit_width(PhysicalRegister::MaxRegistersPerClass - size_t{1});
        constexpr uint32_t RegisterNumberMask =
            (uint32_t{1} << RegisterNumberBits) - 1;

        void require_constraint(bool condition, const char *message)
        {
            if(!condition)
            {
                fatal(message);
            }
        }

        uint32_t encode_register(PhysicalRegister reg)
        {
            return (static_cast<uint32_t>(reg.register_class())
                    << RegisterNumberBits) |
                   reg.number();
        }

        bool requirement_matches(RegisterRequirement requirement,
                                 RegisterClass expected_class)
        {
            switch(requirement.kind())
            {
                case RegisterRequirement::Kind::Any:
                    return requirement.register_class() == expected_class;
                case RegisterRequirement::Kind::Fixed:
                    return requirement.fixed_register().register_class() ==
                           expected_class;
                case RegisterRequirement::Kind::SameAsInput:
                    return true;
            }
            return false;
        }

        std::optional<PhysicalRegister>
        fixed_register_for(RegisterRequirement requirement)
        {
            if(requirement.kind() == RegisterRequirement::Kind::Fixed)
            {
                return requirement.fixed_register();
            }
            return std::nullopt;
        }
    }  // namespace

    RegisterRequirement RegisterRequirement::any(RegisterClass register_class)
    {
        require_constraint(register_class < RegisterClass::Count,
                           "invalid register class requirement");
        return RegisterRequirement(Kind::Any,
                                   static_cast<uint32_t>(register_class));
    }

    RegisterRequirement RegisterRequirement::fixed(PhysicalRegister reg)
    {
        return RegisterRequirement(Kind::Fixed, encode_register(reg));
    }

    RegisterRequirement
    RegisterRequirement::same_as_input(uint32_t operand_index)
    {
        return RegisterRequirement(Kind::SameAsInput, operand_index);
    }

    RegisterClass RegisterRequirement::register_class() const
    {
        require_constraint(kind_ == Kind::Any,
                           "register_class() requires an Any requirement");
        return static_cast<RegisterClass>(payload_);
    }

    PhysicalRegister RegisterRequirement::fixed_register() const
    {
        require_constraint(kind_ == Kind::Fixed,
                           "fixed_register() requires a Fixed requirement");
        return PhysicalRegister(
            static_cast<RegisterClass>(payload_ >> RegisterNumberBits),
            static_cast<uint8_t>(payload_ & RegisterNumberMask));
    }

    uint32_t RegisterRequirement::input_index() const
    {
        require_constraint(kind_ == Kind::SameAsInput,
                           "input_index() requires a SameAsInput requirement");
        return payload_;
    }

    ProgramValueUseConstraint::ProgramValueUseConstraint(
        uint32_t operand_index, AccessTiming timing,
        RegisterRequirement requirement)
        : operand_index(operand_index), timing(timing), requirement(requirement)
    {
        require_constraint(
            requirement.kind() != RegisterRequirement::Kind::SameAsInput,
            "a JIT input cannot have a SameAsInput register requirement");
    }

    TemporaryConstraint::TemporaryConstraint(RegisterRequirement requirement)
        : requirement(requirement)
    {
        require_constraint(
            requirement.kind() != RegisterRequirement::Kind::SameAsInput,
            "a JIT temporary cannot have a SameAsInput register requirement");
    }

    RegisterClass
    register_class_for_representation(ValueRepresentation representation)
    {
        switch(representation)
        {
            case ValueRepresentation::TaggedValue:
                return RegisterClass::GPR;
            case ValueRepresentation::F64:
                return RegisterClass::SIMD;
            case ValueRepresentation::None:
            case ValueRepresentation::Count:
                break;
        }
        fatal("JIT value representation has no register class");
    }

    ProgramValueUseConstraint
    default_program_value_use_constraint(uint32_t operand_index,
                                         ValueRepresentation representation)
    {
        return ProgramValueUseConstraint(
            operand_index, AccessTiming::Early,
            RegisterRequirement::any(
                register_class_for_representation(representation)));
    }

    ResultConstraint
    default_result_constraint(ValueRepresentation representation)
    {
        return ResultConstraint{
            AccessTiming::Late,
            RegisterRequirement::any(
                register_class_for_representation(representation))};
    }

    InstructionAllocationConstraints::InstructionAllocationConstraints(
        const Instruction *instruction,
        std::vector<ProgramValueUseConstraint> input_overrides,
        std::optional<ResultConstraint> result_override,
        std::vector<TemporaryConstraint> temporaries, RegisterSet clobbers)
        : instruction_(instruction),
          input_overrides_(std::move(input_overrides)),
          result_override_(std::move(result_override)),
          temporaries_(std::move(temporaries)), clobbers_(clobbers)
    {
#ifndef NDEBUG
        validate();
#endif
    }

    void InstructionAllocationConstraints::validate() const
    {
        const Instruction *instruction = instruction_;
        require_constraint(instruction != nullptr,
                           "JIT allocation constraints require an instruction");

        struct OperandInfo
        {
            OperandClass operand_class;
            ValueRepresentation representation;
            bool has_override = false;
        };

        std::vector<OperandInfo> operands;
        operands.reserve(instruction->operand_count());
        visit_operand_references(
            *instruction,
            [&](uint32_t operand_index, OperandClass operand_class,
                ValueRepresentation representation, Instruction *) {
                require_constraint(operand_index == operands.size(),
                                   "JIT operand traversal is not contiguous");
                operands.push_back({operand_class, representation, false});
            });

        for(const ProgramValueUseConstraint &input: input_overrides_)
        {
            require_constraint(input.operand_index < operands.size(),
                               "JIT input constraint names no operand");
            OperandInfo &operand = operands[input.operand_index];
            require_constraint(
                operand.operand_class == OperandClass::ProgramValue &&
                    operand.representation != ValueRepresentation::None,
                "JIT input constraint does not name an allocatable "
                "ProgramValue operand");
            require_constraint(
                !operand.has_override,
                "JIT ProgramValue operand has duplicate input overrides");
            operand.has_override = true;
            require_constraint(
                requirement_matches(
                    input.requirement,
                    register_class_for_representation(operand.representation)),
                "JIT input requirement has the wrong register class");
        }

        if(result_override_.has_value())
        {
            require_constraint(
                instruction->result_class() == ResultClass::ProgramValue,
                "non-ProgramValue JIT instruction cannot have a result "
                "override");
            RegisterRequirement requirement = result_override_->requirement;
            RegisterClass result_class = register_class_for_representation(
                instruction->value_representation());
            require_constraint(
                requirement_matches(requirement, result_class),
                "JIT result requirement has the wrong register class");

            if(requirement.kind() == RegisterRequirement::Kind::SameAsInput)
            {
                uint32_t input_index = requirement.input_index();
                require_constraint(
                    input_index < operands.size() &&
                        operands[input_index].operand_class ==
                            OperandClass::ProgramValue &&
                        operands[input_index].representation !=
                            ValueRepresentation::None,
                    "SameAsInput names no allocatable ProgramValue operand");
                require_constraint(
                    operands[input_index].representation ==
                        instruction->value_representation(),
                    "SameAsInput names an operand with a different value "
                    "representation");
            }
        }

        for(const TemporaryConstraint &temporary: temporaries_)
        {
            std::optional<PhysicalRegister> fixed =
                fixed_register_for(temporary.requirement);
            require_constraint(!fixed.has_value() ||
                                   !clobbers_.contains(*fixed),
                               "JIT clobber collides with a fixed temporary");
        }

        for(const ProgramValueUseConstraint &input: input_overrides_)
        {
            std::optional<PhysicalRegister> fixed =
                fixed_register_for(input.requirement);
            if(input.timing == AccessTiming::Late && fixed.has_value())
            {
                require_constraint(
                    !clobbers_.contains(*fixed),
                    "JIT clobber collides with a fixed late input");
            }
        }

        if(result_override_.has_value())
        {
            std::optional<PhysicalRegister> fixed =
                fixed_register_for(result_override_->requirement);
            if(result_override_->requirement.kind() ==
               RegisterRequirement::Kind::SameAsInput)
            {
                uint32_t input_index =
                    result_override_->requirement.input_index();
                for(const ProgramValueUseConstraint &input: input_overrides_)
                {
                    if(input.operand_index == input_index)
                    {
                        fixed = fixed_register_for(input.requirement);
                        break;
                    }
                }
            }
            require_constraint(!fixed.has_value() ||
                                   !clobbers_.contains(*fixed),
                               "JIT clobber collides with a fixed result");
        }
    }

}  // namespace cl::jit
