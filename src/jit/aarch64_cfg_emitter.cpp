#include "jit/aarch64_cfg_emitter.h"

#include "jit/aarch64_assembler.h"
#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/location_assignments.h"
#include "runtime/fatal.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cl::jit
{
    namespace
    {
        XRegister assigned_register(const LocationAssignments &locations,
                                    ProgramValueRef value)
        {
            PhysicalLocation location = locations.location_for(value);
            if(!location.is_register())
            {
                fatal("AArch64 emitter requires a register-mapped value");
            }
            PhysicalRegister reg = location.reg();
            if(reg.register_class() != RegisterClass::GPR)
            {
                fatal("AArch64 emitter requires a GPR-mapped value");
            }
            return XRegister(reg.number());
        }

        XRegister assigned_temporary(const LocationAssignments &locations,
                                     Instruction instruction,
                                     size_t temporary_index)
        {
            PhysicalLocation location =
                locations.location_for(instruction, temporary_index);
            if(!location.is_register())
            {
                fatal("AArch64 emitter requires a register-mapped temporary");
            }
            PhysicalRegister reg = location.reg();
            if(reg.register_class() != RegisterClass::GPR)
            {
                fatal("AArch64 emitter requires a GPR-mapped temporary");
            }
            return XRegister(reg.number());
        }

        StackLocation assigned_stack(const LocationAssignments &locations,
                                     ProgramValueRef value)
        {
            PhysicalLocation location = locations.location_for(value);
            if(!location.is_stack())
            {
                fatal("AArch64 emitter requires a stack-mapped value");
            }
            return location.stack();
        }

        int64_t stack_byte_offset(StackLocation stack)
        {
            static_assert(sizeof(Value) == 8);
            return static_cast<int64_t>(stack.frame_offset()) * sizeof(Value);
        }

        void emit_smi_logical(AArch64MacroAssembler &assembler,
                              const LocationAssignments &locations,
                              LogicalOp operation, ProgramValueRef result,
                              ProgramValueRef lhs, ProgramValueRef rhs)
        {
            assembler.emit_logical_reg(operation,
                                       assigned_register(locations, result),
                                       assigned_register(locations, lhs),
                                       assigned_register(locations, rhs));
        }

        void emit_identity_test(AArch64MacroAssembler &assembler,
                                const LocationAssignments &locations,
                                AArch64Condition condition,
                                Instruction instruction, ProgramValueRef lhs,
                                ProgramValueRef rhs)
        {
            XRegister result =
                assigned_register(locations, ProgramValueRef(instruction));
            XRegister temporary = assigned_temporary(locations, instruction, 0);
            assembler.cmp(assigned_register(locations, lhs),
                          assigned_register(locations, rhs));
            assembler.mov(result,
                          static_cast<uint64_t>(Value::False().as.integer));
            assembler.mov(temporary,
                          static_cast<uint64_t>(Value::True().as.integer));
            assembler.emit_conditional_select(condition, result, temporary,
                                              result);
        }

        Result<CodeAllocation, JitCodeError>
        generate_allocation(const ControlFlowGraph &graph, CodeCache &cache,
                            const LocationAssignments &locations,
                            AArch64ValuePoolMode pool_mode)
        {
            AArch64MacroAssembler assembler(pool_mode);
            generate_aarch64_assembly(graph, locations, assembler);
            return assembler.emitter().finalize(cache);
        }
    }  // namespace

    void generate_aarch64_assembly(const ControlFlowGraph &graph,
                                   const LocationAssignments &locations,
                                   AArch64MacroAssembler &assembler)
    {
        assert(graph.is_published());
        assert(graph.blocks().size() == 1);

        const Block *entry = graph.entry_block();
        assert(entry != nullptr);
        assert(graph.blocks()[0] == entry);
        assert(entry->predecessor_edges().empty());
        for(InstructionId parameter_id: entry->parameters())
        {
            Instruction parameter = graph.storage()->instruction(parameter_id);
            assert(parameter.kind() == InstructionKind::Parameter);
        }

        for(InstructionId instruction_id: entry->instructions())
        {
            Instruction instruction =
                graph.storage()->instruction(instruction_id);
            // clang-format off
            CL_JIT_INSTRUCTION_SWITCH(instruction)
            {
                case InstructionKind::Uninitialized:
                    break;

                case CL_JIT_INSTRUCTION_CASE(ConstInstruction,
                                             constant_instruction)
                {
                    Value constant = constant_instruction.constant();
                    XRegister destination =
                        assigned_register(locations,
                                          ProgramValueRef(instruction));
                    if(constant.is_inline())
                    {
                        assembler.mov(
                            destination,
                            static_cast<uint64_t>(constant.as.integer));
                    }
                    else
                    {
                        assembler.ldr(destination, constant);
                    }
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(AndSMIInstruction,
                                             and_instruction)
                {
                    emit_smi_logical(
                        assembler, locations, LogicalOp::And,
                        ProgramValueRef(instruction), and_instruction.lhs(),
                        and_instruction.rhs());
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(OrrSMIInstruction,
                                             orr_instruction)
                {
                    emit_smi_logical(
                        assembler, locations, LogicalOp::Orr,
                        ProgramValueRef(instruction), orr_instruction.lhs(),
                        orr_instruction.rhs());
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(EorSMIInstruction,
                                             eor_instruction)
                {
                    emit_smi_logical(
                        assembler, locations, LogicalOp::Eor,
                        ProgramValueRef(instruction), eor_instruction.lhs(),
                        eor_instruction.rhs());
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(IsInstruction, is_instruction)
                {
                    emit_identity_test(
                        assembler, locations, AArch64Condition::Equal,
                        instruction, is_instruction.lhs(), is_instruction.rhs());
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(IsNotInstruction,
                                             is_not_instruction)
                {
                    emit_identity_test(
                        assembler, locations, AArch64Condition::NotEqual,
                        instruction, is_not_instruction.lhs(),
                        is_not_instruction.rhs());
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(MovInstruction, move_instruction)
                {
                    assembler.mov(
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        assigned_register(locations,
                                          move_instruction.source()));
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(LoadStackInstruction,
                                             load_instruction)
                {
                    constexpr XRegister FramePointer(29);
                    assembler.ldr(
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        FramePointer,
                        stack_byte_offset(assigned_stack(
                            locations, load_instruction.source())));
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(StoreStackInstruction,
                                             store_instruction)
                {
                    constexpr XRegister FramePointer(29);
                    assembler.str(
                        assigned_register(locations,
                                          store_instruction.source()),
                        FramePointer,
                        stack_byte_offset(assigned_stack(
                            locations, ProgramValueRef(instruction))));
                    break;
                }

                case InstructionKind::LoadStackF64:
                case InstructionKind::StoreStackF64:
                    fatal("AArch64 F64 stack transfer emission is not "
                          "implemented");

                case CL_JIT_INSTRUCTION_CASE(ReturnInstruction,
                                             return_instruction)
                {
                    (void)assigned_register(
                        locations, return_instruction.return_value());
                    assembler.emit_ret();
                    break;
                }

                default:
                    assert(false);
            }
            // clang-format on
        }
    }

    Result<PublishedCode, JitCodeError>
    emit_aarch64_from_cfg(const ControlFlowGraph &graph,
                          const LocationAssignments &locations,
                          CodeCache &cache)
    {
        Result<CodeAllocation, JitCodeError> near = generate_allocation(
            graph, cache, locations, AArch64ValuePoolMode::NearLiteral);
        if(near)
        {
            return cache.publish(std::move(near).value());
        }
        if(near.error() != JitCodeError::PoolOutOfRange)
        {
            return Result<PublishedCode, JitCodeError>::error(near.error());
        }

        Result<CodeAllocation, JitCodeError> far = generate_allocation(
            graph, cache, locations, AArch64ValuePoolMode::FarPageRelative);
        if(!far)
        {
            return Result<PublishedCode, JitCodeError>::error(far.error());
        }
        return cache.publish(std::move(far).value());
    }

}  // namespace cl::jit
