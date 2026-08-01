#include "jit/aarch64_allocation_constraints.h"

#include "jit/compilation_storage.h"
#include "jit/control_flow_graph.h"
#include "runtime/fatal.h"

#include <array>
#include <cassert>
#include <optional>
#include <span>
#include <utility>
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
            gpr(12), gpr(13), gpr(14), gpr(15), gpr(19), gpr(20),
            gpr(22), gpr(23), gpr(24), gpr(26), gpr(27), gpr(28),
        };
        constexpr std::array GPRScratchRegisters = {gpr(16), gpr(17)};

        constexpr std::array SIMDAllocationOrder = {
            simd(0),  simd(1),  simd(2),  simd(3),  simd(4),  simd(5),
            simd(6),  simd(7),  simd(16), simd(17), simd(18), simd(19),
            simd(20), simd(21), simd(22), simd(23), simd(24), simd(25),
            simd(26), simd(27), simd(28), simd(29),
        };
        constexpr std::array SIMDScratchRegisters = {simd(30), simd(31)};

        [[noreturn]] void unsupported_instruction(InstructionKind kind)
        {
            (void)kind;
            fatal("unsupported instruction in AArch64 allocation constraint "
                  "bring-up");
        }

        InstructionAllocationConstraints entry_parameter_constraints(
            Instruction parameter, size_t parameter_index,
            const std::optional<BytecodeStateOrder> &bytecode_state_order)
        {
            if(bytecode_state_order.has_value() &&
               parameter_index >= bytecode_state_order->n_parameters())
            {
                size_t frame_header_index =
                    parameter_index - bytecode_state_order->n_parameters();
                if(frame_header_index >= FrameHeaderSize)
                {
                    fatal("AArch64 bytecode entry has an unexpected "
                          "parameter");
                }

                int32_t frame_offset =
                    FrameHeaderPreviousFpOffset + int32_t(frame_header_index);
                bool pointer = frame_header_value_is_pointer(frame_offset);
                if((pointer &&
                    parameter.kind() != InstructionKind::ParameterPointer) ||
                   (!pointer && parameter.kind() != InstructionKind::Parameter))
                {
                    fatal("AArch64 bytecode frame-header parameter has the "
                          "wrong representation");
                }
                return InstructionAllocationConstraints(
                    parameter, {},
                    ResultConstraint{
                        AccessTiming::Late,
                        LocationRequirement::fixed(PhysicalLocation::stack(
                            StackLocation(StackLocationKind::LocalOrTemporary,
                                          frame_offset)))});
            }

            if(parameter.kind() != InstructionKind::Parameter)
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

        InstructionAllocationConstraints return_constraints(
            ReturnInstruction instruction,
            std::vector<ProgramValueUseConstraint> input_overrides)
        {
            input_overrides.emplace_back(
                ReturnInstruction::return_value_operand_index,
                AccessTiming::Early,
                LocationRequirement::fixed(PhysicalLocation::reg(gpr(0))));
            input_overrides.emplace_back(
                ReturnInstruction::previous_frame_pointer_operand_index,
                AccessTiming::Early, LocationRequirement::any_location());
            input_overrides.emplace_back(
                ReturnInstruction::return_code_object_operand_index,
                AccessTiming::Early,
                LocationRequirement::fixed(PhysicalLocation::reg(gpr(24))));
            input_overrides.emplace_back(
                ReturnInstruction::return_pc_operand_index, AccessTiming::Early,
                LocationRequirement::fixed(PhysicalLocation::reg(gpr(22))));
            return InstructionAllocationConstraints(instruction,
                                                    std::move(input_overrides));
        }

        InstructionAllocationConstraints bare_return_constraints(
            BareReturnInstruction instruction,
            std::vector<ProgramValueUseConstraint> input_overrides)
        {
            input_overrides.emplace_back(
                BareReturnInstruction::return_value_operand_index,
                AccessTiming::Early,
                LocationRequirement::fixed(PhysicalLocation::reg(gpr(0))));
            return InstructionAllocationConstraints(instruction,
                                                    std::move(input_overrides));
        }

        InstructionAllocationConstraints gpr_temporary_constraints(
            Instruction instruction,
            std::vector<ProgramValueUseConstraint> input_overrides)
        {
            return InstructionAllocationConstraints(
                instruction, std::move(input_overrides), std::nullopt,
                {TemporaryConstraint(
                    LocationRequirement::any_register(RegisterClass::GPR))});
        }

        std::vector<ProgramValueUseConstraint>
        side_exit_argument_constraints(Instruction instruction)
        {
            std::vector<ProgramValueUseConstraint> result;
            uint32_t first_argument = instruction.side_exit_argument_start();
            if(first_argument == InstructionFamilyMetadata::NoSideExitArguments)
            {
                return result;
            }

            visit_operand_references(
                instruction,
                [&](uint32_t operand_index, OperandClass operand_class,
                    ValueRepresentationRequirement, InstructionId) {
                    if(operand_index < first_argument)
                    {
                        return;
                    }
                    if(operand_class != OperandClass::ProgramValue)
                    {
                        fatal("AArch64 side-exit argument is not a program "
                              "value");
                    }
                    result.emplace_back(operand_index, AccessTiming::Late,
                                        LocationRequirement::any_location());
                });
            return result;
        }
    }  // namespace

    AllocationConstraints
    make_aarch64_allocation_constraints(const ControlFlowGraph &graph)
    {
        assert(graph.is_published());
        assert(graph.ir_level() == IRLevel::Machine);
        const Block *entry = graph.entry_block();
        assert(entry != nullptr);

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.reserve(entry->parameters().size() + graph.blocks().size());

        for(size_t index = 0; index < entry->parameters().size(); ++index)
        {
            overrides.push_back(
                entry_parameter_constraints(entry->parameter_at(index), index,
                                            graph.bytecode_state_order()));
        }

        for(const Block *block: graph.blocks())
        {
            assert(block != nullptr);

            for(Instruction instruction: block->instructions())
            {
                std::vector<ProgramValueUseConstraint> input_overrides =
                    side_exit_argument_constraints(instruction);
                // clang-format off
                CL_JIT_MACHINE_INSTRUCTION_SWITCH(instruction)
                {
                    case MachineInstructionKind::Const:
                    case MachineInstructionKind::Uninitialized:
                    case MachineInstructionKind::AddSMIWithSideExit:
                    case MachineInstructionKind::SubSMIWithSideExit:
                    case MachineInstructionKind::AndSMI:
                    case MachineInstructionKind::OrrSMI:
                    case MachineInstructionKind::EorSMI:
                    case MachineInstructionKind::MovPointer:
                    case MachineInstructionKind::LoadStackPointer:
                    case MachineInstructionKind::StoreStackPointer:
                    case MachineInstructionKind::InlineTagGuardWithSideExit:
                    case MachineInstructionKind::ResumeInInterpreterWithSideExit:
                    case MachineInstructionKind::ExitToInterpreter:
                    case MachineInstructionKind::ConditionalBranch:
                    case MachineInstructionKind::UnconditionalBranch:
                        if(!input_overrides.empty())
                        {
                            overrides.emplace_back(instruction,
                                                   std::move(input_overrides));
                        }
                        break;

                    case MachineInstructionKind::MulSMIWithSideExit:
                        input_overrides.emplace_back(
                            MulSMIWithSideExitInstruction::lhs_operand_index,
                            AccessTiming::Late,
                            LocationRequirement::any_register(
                                RegisterClass::GPR));
                        input_overrides.emplace_back(
                            MulSMIWithSideExitInstruction::rhs_operand_index,
                            AccessTiming::Late,
                            LocationRequirement::any_register(
                                RegisterClass::GPR));
                        overrides.push_back(gpr_temporary_constraints(
                            instruction, std::move(input_overrides)));
                        break;

                    case MachineInstructionKind::Is:
                    case MachineInstructionKind::IsNot:
                        overrides.push_back(gpr_temporary_constraints(
                            instruction, std::move(input_overrides)));
                        break;

                    case CL_JIT_MACHINE_INSTRUCTION_CASE(
                        ReturnInstruction, return_instruction)
                    {
                        overrides.push_back(return_constraints(
                            return_instruction,
                            std::move(input_overrides)));
                        break;
                    }

                    case CL_JIT_MACHINE_INSTRUCTION_CASE(
                        BareReturnInstruction, return_instruction)
                    {
                        overrides.push_back(bare_return_constraints(
                            return_instruction,
                            std::move(input_overrides)));
                        break;
                    }

                    default:
                        unsupported_instruction(instruction.kind());
                }
                // clang-format on
            }
        }

        std::vector<RegisterClassDefinition> register_classes;
        register_classes.emplace_back(RegisterClass::GPR, GPRAllocationOrder,
                                      GPRScratchRegisters);
        register_classes.emplace_back(RegisterClass::SIMD, SIMDAllocationOrder,
                                      SIMDScratchRegisters);
        return AllocationConstraints(std::move(register_classes),
                                     std::move(overrides));
    }

}  // namespace cl::jit
