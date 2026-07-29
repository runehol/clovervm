#include "jit/aarch64_cfg_emitter.h"

#include "jit/aarch64_assembler.h"
#include "jit/aarch64_transition.h"
#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/location_assignments.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct PendingSideExit
        {
            Label label;
            SideExitId side_exit;
            ProgramValueRefRange arguments;
        };

        using BlockLabels = absl::flat_hash_map<const Block *, Label>;

        Label block_label(const BlockLabels &labels, const Block *block)
        {
            assert(block != nullptr);
            return labels.at(block);
        }

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

        void emit_inline_tag_test(AArch64MacroAssembler &assembler,
                                  XRegister source,
                                  InlineValueClass expected_class)
        {
            switch(expected_class)
            {
                case InlineValueClass::SMI:
                    assembler.tst(
                        source, inline_value_class_mask(InlineValueClass::SMI));
                    return;
                case InlineValueClass::Boolean:
                    assembler.emit_logical_imm(
                        LogicalOp::And, XRegister(16), source,
                        inline_value_class_mask(InlineValueClass::Boolean));
                    assembler.cmp(XRegister(16),
                                  inline_value_class_expected_bits(
                                      InlineValueClass::Boolean));
                    return;
                case InlineValueClass::SMIOrBoolean:
                    assembler.mov(XRegister(16),
                                  inline_value_class_mask(
                                      InlineValueClass::SMIOrBoolean));
                    assembler.tst(source, XRegister(16));
                    return;
            }
            assert(false);
        }

        void emit_truthiness_branch(AArch64MacroAssembler &assembler,
                                    XRegister condition,
                                    AArch64Condition branch_condition,
                                    Label target)
        {
            assert(branch_condition == AArch64Condition::Equal ||
                   branch_condition == AArch64Condition::NotEqual);
            assembler.tst(condition, value_truthy_mask);
            assembler.b(branch_condition, target);
        }

        Result<PublishedCode, JitCodeError>
        generate_code(const ControlFlowGraph &graph, CodeCache &cache,
                      const LocationAssignments &locations,
                      AArch64ValuePoolMode pool_mode,
                      MachineAddress side_exit_thunk)
        {
            AArch64MacroAssembler assembler(pool_mode);
            generate_aarch64_assembly(graph, locations, assembler,
                                      side_exit_thunk);
            CodeAllocation allocation =
                CL_TRY(assembler.emitter().finalize(cache));
            size_t tagged_value_count =
                assembler.emitter().tagged_value_count();
            CL_TRY(cache.publish(allocation));
            return Result<PublishedCode, JitCodeError>::ok(PublishedCode(
                allocation.code, allocation.constant_pool(),
                allocation.constant_pool_address(), tagged_value_count,
                allocation.encoded_code_size()));
        }
    }  // namespace

    void generate_aarch64_assembly(const ControlFlowGraph &graph,
                                   const LocationAssignments &locations,
                                   AArch64MacroAssembler &assembler,
                                   MachineAddress side_exit_thunk)
    {
        assert(graph.is_published());
        assert(graph.ir_level() == IRLevel::Machine);

        const Block *entry = graph.entry_block();
        assert(entry != nullptr);
        assert(graph.blocks()[0] == entry);
        assert(entry->predecessor_edges().empty());
        (void)entry;

        BlockLabels block_labels;
        block_labels.reserve(graph.blocks().size());
        for(const Block *block: graph.blocks())
        {
            bool inserted =
                block_labels.emplace(block, assembler.emitter().make_label())
                    .second;
            assert(inserted);
            (void)inserted;
        }

        std::vector<PendingSideExit> pending_side_exits;
        auto side_exit_target = [&](SideExitId side_exit,
                                    ProgramValueRefRange arguments) {
            Label label = assembler.emitter().make_label();
            pending_side_exits.push_back({label, side_exit, arguments});
            return label;
        };

        auto emit_instruction = [&](Instruction instruction,
                                    const Block *next_block) {
            // clang-format off
            CL_JIT_MACHINE_INSTRUCTION_SWITCH(instruction)
            {
                case MachineInstructionKind::Uninitialized:
                    break;

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    ConstInstruction, constant_instruction)
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

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    AndSMIInstruction, and_instruction)
                {
                    emit_smi_logical(
                        assembler, locations, LogicalOp::And,
                        ProgramValueRef(instruction), and_instruction.lhs(),
                        and_instruction.rhs());
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    OrrSMIInstruction, orr_instruction)
                {
                    emit_smi_logical(
                        assembler, locations, LogicalOp::Orr,
                        ProgramValueRef(instruction),
                        orr_instruction.lhs(), orr_instruction.rhs());
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    EorSMIInstruction, eor_instruction)
                {
                    emit_smi_logical(
                        assembler, locations, LogicalOp::Eor,
                        ProgramValueRef(instruction),
                        eor_instruction.lhs(), eor_instruction.rhs());
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    IsInstruction, is_instruction)
                {
                    emit_identity_test(
                        assembler, locations, AArch64Condition::Equal,
                        instruction, is_instruction.lhs(),
                        is_instruction.rhs());
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    IsNotInstruction, is_not_instruction)
                {
                    emit_identity_test(
                        assembler, locations, AArch64Condition::NotEqual,
                        instruction, is_not_instruction.lhs(),
                        is_not_instruction.rhs());
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    MovInstruction, move_instruction)
                {
                    assembler.mov(
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        assigned_register(locations,
                                          move_instruction.source()));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    MovPointerInstruction, move_pointer_instruction)
                {
                    assembler.mov(
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        assigned_register(
                            locations,
                            move_pointer_instruction.source()));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    LoadStackInstruction, load_instruction)
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

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    LoadStackPointerInstruction, load_pointer_instruction)
                {
                    constexpr XRegister FramePointer(29);
                    assembler.ldr(
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        FramePointer,
                        stack_byte_offset(assigned_stack(
                            locations,
                            load_pointer_instruction.source())));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    StoreStackInstruction, store_instruction)
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

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    StoreStackPointerInstruction,
                    store_pointer_instruction)
                {
                    constexpr XRegister FramePointer(29);
                    assembler.str(
                        assigned_register(
                            locations,
                            store_pointer_instruction.source()),
                        FramePointer,
                        stack_byte_offset(assigned_stack(
                            locations, ProgramValueRef(instruction))));
                    break;
                }

                case MachineInstructionKind::LoadStackF64:
                case MachineInstructionKind::StoreStackF64:
                    fatal("AArch64 F64 stack transfer emission is not "
                          "implemented");

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    AddSMIWithSideExitInstruction, add_instruction)
                {
                    assembler.emit_arithmetic_reg(
                        ArithmeticOp::Adds,
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        assigned_register(locations,
                                          add_instruction.lhs()),
                        assigned_register(locations,
                                          add_instruction.rhs()));
                    assembler.b(
                        AArch64Condition::Overflow,
                        side_exit_target(
                            add_instruction.side_exit(),
                            add_instruction.side_exit_arguments()));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    InlineTagGuardWithSideExitInstruction,
                    guard_instruction)
                {
                    XRegister input = assigned_register(
                        locations, guard_instruction.value());
                    XRegister result = assigned_register(
                        locations, ProgramValueRef(instruction));
                    emit_inline_tag_test(
                        assembler, input,
                        guard_instruction.expected_class());
                    Label target = side_exit_target(
                        guard_instruction.side_exit(),
                        guard_instruction.side_exit_arguments());
                    assembler.b(AArch64Condition::NotEqual, target);
                    if(result.encoding() != input.encoding())
                    {
                        assembler.mov(result, input);
                    }
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    ConditionalBranchInstruction, branch_instruction)
                {
                    const Block *true_target =
                        branch_instruction.true_edge()->target();
                    const Block *false_target =
                        branch_instruction.false_edge()->target();
                    if(true_target == false_target)
                    {
                        if(true_target != next_block)
                        {
                            assembler.b(
                                block_label(block_labels, true_target));
                        }
                        break;
                    }
                    XRegister condition = assigned_register(
                        locations, branch_instruction.condition());
                    if(true_target == next_block)
                    {
                        emit_truthiness_branch(
                            assembler, condition,
                            AArch64Condition::Equal,
                            block_label(block_labels, false_target));
                        break;
                    }
                    emit_truthiness_branch(
                        assembler, condition,
                        AArch64Condition::NotEqual,
                        block_label(block_labels, true_target));
                    if(false_target != next_block)
                    {
                        assembler.b(
                            block_label(block_labels, false_target));
                    }
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    UnconditionalBranchInstruction, branch_instruction)
                {
                    const Block *target =
                        branch_instruction.edge()->target();
                    if(target != next_block)
                    {
                        assembler.b(block_label(block_labels, target));
                    }
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    ResumeInInterpreterWithSideExitInstruction,
                    resume_instruction)
                {
                    assembler.b(side_exit_target(
                        resume_instruction.side_exit(),
                        resume_instruction.side_exit_arguments()));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    ReturnInstruction, return_instruction)
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
        };

        for(size_t block_index = 0; block_index < graph.blocks().size();
            ++block_index)
        {
            const Block *block = graph.blocks()[block_index];
            const Block *next_block = block_index + 1 == graph.blocks().size()
                                          ? nullptr
                                          : graph.blocks()[block_index + 1];
            assembler.emitter().resolve(block_label(block_labels, block));

            for(Instruction parameter: block->parameters())
            {
                assert(is_block_parameter_kind(parameter.kind()));
                (void)parameter;
            }
            for(Instruction instruction: block->instructions())
            {
                emit_instruction(instruction, next_block);
            }
        }

        if(!pending_side_exits.empty())
        {
            assert(graph.bytecode_state_order().has_value());
        }
        for(const PendingSideExit &pending: pending_side_exits)
        {
            assembler.emitter().resolve(pending.label);
            const SideExit &side_exit = graph.side_exit(pending.side_exit);
            std::vector<TransitionInstruction> program =
                emit_aarch64_side_exit_transition_program(
                    *graph.storage(), *graph.bytecode_state_order(), side_exit,
                    pending.arguments, locations);
            ConstantPoolEntry entry = assembler.add_transition_program(program);
            assembler.adr(XRegister(16), entry);
            assembler.b(side_exit_thunk, XRegister(17));
        }
    }

    Result<PublishedCode, JitCodeError>
    emit_aarch64_from_cfg(const ControlFlowGraph &graph,
                          const LocationAssignments &locations,
                          CodeCache &cache, MachineAddress side_exit_thunk)
    {
        Result<PublishedCode, JitCodeError> near =
            generate_code(graph, cache, locations,
                          AArch64ValuePoolMode::NearLiteral, side_exit_thunk);
        if(near)
        {
            return near;
        }
        if(near.error() != JitCodeError::PoolOutOfRange)
        {
            return Result<PublishedCode, JitCodeError>::error(near.error());
        }

        Result<PublishedCode, JitCodeError> far = generate_code(
            graph, cache, locations, AArch64ValuePoolMode::FarPageRelative,
            side_exit_thunk);
        if(!far)
        {
            return Result<PublishedCode, JitCodeError>::error(far.error());
        }
        return far;
    }

}  // namespace cl::jit
