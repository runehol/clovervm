#include "jit/aarch64_allocation_constraints.h"

#include "jit/compilation_storage.h"
#include "jit/control_flow_graph.h"
#include "runtime/fatal.h"

#include <array>
#include <cassert>
#include <span>
#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr uint8_t PlatformIntegerArgumentRegisterCount = 8;

        constexpr PhysicalRegister gpr(uint8_t number)
        {
            return PhysicalRegister(RegisterClass::GPR, number);
        }

        constexpr PhysicalRegister simd(uint8_t number)
        {
            return PhysicalRegister(RegisterClass::SIMD, number);
        }

        constexpr std::array GPRAllocationOrder = {
            gpr(0),  gpr(1),  gpr(2),  gpr(3),  gpr(4),  gpr(5),
            gpr(6),  gpr(7),  gpr(8),  gpr(9),  gpr(10), gpr(11),
            gpr(12), gpr(13), gpr(14), gpr(15),
        };

        constexpr std::array SIMDAllocationOrder = {
            simd(0),  simd(1),  simd(2),  simd(3),  simd(4),  simd(5),
            simd(6),  simd(7),  simd(16), simd(17), simd(18), simd(19),
            simd(20), simd(21), simd(22), simd(23), simd(24), simd(25),
            simd(26), simd(27), simd(28), simd(29), simd(30),
        };

        [[noreturn]] void unsupported_instruction(InstructionKind kind)
        {
            (void)kind;
            fatal("unsupported instruction in AArch64 allocation constraint "
                  "bring-up");
        }

        InstructionAllocationConstraints
        entry_parameter_constraints(const Instruction *parameter,
                                    size_t parameter_index)
        {
            if(parameter->kind() != InstructionKind::Parameter)
            {
                fatal("AArch64 allocation constraint bring-up does not "
                      "support F64 entry parameters");
            }
            if(parameter_index >= PlatformIntegerArgumentRegisterCount)
            {
                fatal("AArch64 allocation constraint bring-up does not "
                      "support stack-passed entry parameters");
            }
            return InstructionAllocationConstraints(
                parameter, {},
                ResultConstraint{
                    AccessTiming::Late,
                    LocationRequirement::fixed(PhysicalLocation::reg(
                        gpr(static_cast<uint8_t>(parameter_index))))});
        }

        InstructionAllocationConstraints
        return_constraints(const ReturnInstruction *instruction)
        {
            return InstructionAllocationConstraints(
                instruction,
                {{ReturnInstruction::return_value_operand_index,
                  AccessTiming::Early,
                  LocationRequirement::fixed(PhysicalLocation::reg(gpr(0)))}});
        }

        InstructionAllocationConstraints
        gpr_temporary_constraints(const Instruction *instruction)
        {
            return InstructionAllocationConstraints(
                instruction, {}, std::nullopt,
                {TemporaryConstraint(
                    LocationRequirement::any_register(RegisterClass::GPR))});
        }
    }  // namespace

    AllocationConstraints
    make_aarch64_allocation_constraints(const ControlFlowGraph &graph)
    {
        assert(graph.is_published());
        const Block *entry = graph.entry_block();
        assert(entry != nullptr);

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.reserve(entry->parameters().size() + graph.blocks().size());

        for(size_t index = 0; index < entry->parameters().size(); ++index)
        {
            overrides.push_back(
                entry_parameter_constraints(entry->parameter_at(index), index));
        }

        for(const Block *block: graph.blocks())
        {
            assert(block != nullptr);

            for(InstructionId instruction_id: block->instructions())
            {
                const Instruction *instruction =
                    graph.storage()->instruction(instruction_id);
                switch(instruction->kind())
                {
                    case InstructionKind::Const:
                    case InstructionKind::Uninitialized:
                    case InstructionKind::AndSMI:
                    case InstructionKind::OrrSMI:
                    case InstructionKind::EorSMI:
                    case InstructionKind::Snapshot:
                    case InstructionKind::ResumeInInterpreter:
                        break;

                    case InstructionKind::Is:
                    case InstructionKind::IsNot:
                        overrides.push_back(
                            gpr_temporary_constraints(instruction));
                        break;

                    case InstructionKind::Return:
                        overrides.push_back(return_constraints(
                            instruction->as<ReturnInstruction>()));
                        break;

                    case InstructionKind::ConditionalBranch:
                    case InstructionKind::UnconditionalBranch:
                        overrides.push_back(
                            gpr_temporary_constraints(instruction));
                        break;

                    default:
                        unsupported_instruction(instruction->kind());
                }
            }
        }

        std::vector<RegisterClassDefinition> register_classes;
        register_classes.emplace_back(RegisterClass::GPR, GPRAllocationOrder,
                                      gpr(16));
        register_classes.emplace_back(RegisterClass::SIMD, SIMDAllocationOrder,
                                      simd(31));
        return AllocationConstraints(std::move(register_classes),
                                     std::move(overrides));
    }

}  // namespace cl::jit
