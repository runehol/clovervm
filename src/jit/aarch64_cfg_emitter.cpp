#include "jit/aarch64_cfg_emitter.h"

#include "jit/aarch64_assembler.h"
#include "jit/aarch64_jit_registers.h"
#include "jit/aarch64_transition.h"
#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/location_assignments.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct PendingSideExit
        {
            Label label;
            SideExitBinding binding;
        };

        using BlockLabels = absl::flat_hash_map<const Block *, Label>;
        using SideExitLabels = absl::flat_hash_map<SideExitBinding, size_t>;

        enum class ConsumedInstructionCount : size_t
        {
            One = 1,
            Two = 2,
        };

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

        void emit_binary_logical_smi(AArch64MacroAssembler &assembler,
                                     const LocationAssignments &locations,
                                     BinaryLogicalSMIInstruction logical)
        {
            LogicalOp operation = [&] {
                switch(logical.subkind())
                {
                    case BinaryLogicalSMISubkind::AndSMI:
                        return LogicalOp::And;
                    case BinaryLogicalSMISubkind::OrrSMI:
                        return LogicalOp::Orr;
                    case BinaryLogicalSMISubkind::EorSMI:
                        return LogicalOp::Eor;
                }
                __builtin_unreachable();
            }();
            assembler.emit_logical_reg(
                operation,
                assigned_register(locations, ProgramValueRef(logical)),
                assigned_register(locations, logical.lhs()),
                assigned_register(locations, logical.rhs()));
        }

        AArch64Condition
        emit_is_comparison(AArch64MacroAssembler &assembler,
                           const LocationAssignments &locations,
                           IsComparisonInstruction comparison)
        {
            assembler.cmp(assigned_register(locations, comparison.lhs()),
                          assigned_register(locations, comparison.rhs()));
            switch(comparison.subkind())
            {
                case IsComparisonSubkind::Is:
                    return AArch64Condition::Equal;
                case IsComparisonSubkind::IsNot:
                    return AArch64Condition::NotEqual;
            }
            __builtin_unreachable();
        }

        void
        emit_tagged_boolean_from_flags(AArch64MacroAssembler &assembler,
                                       const LocationAssignments &locations,
                                       AArch64Condition true_condition,
                                       Instruction instruction)
        {
            XRegister result =
                assigned_register(locations, ProgramValueRef(instruction));
            XRegister temporary = assigned_temporary(locations, instruction, 0);
            assembler.mov(result,
                          static_cast<uint64_t>(Value::False().as.integer));
            assembler.mov(temporary,
                          static_cast<uint64_t>(Value::True().as.integer));
            assembler.emit_conditional_select(true_condition, result, temporary,
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

        void emit_combined_inline_tag_test(AArch64MacroAssembler &assembler,
                                           XRegister lhs, XRegister rhs,
                                           InlineValueClass expected_class)
        {
            if(lhs.encoding() == rhs.encoding())
            {
                emit_inline_tag_test(assembler, lhs, expected_class);
                return;
            }

            constexpr XRegister Scratch0(16);
            constexpr XRegister Scratch1(17);
            switch(expected_class)
            {
                case InlineValueClass::SMI:
                    assembler.emit_logical_reg(LogicalOp::Orr, Scratch0, lhs,
                                               rhs);
                    assembler.tst(Scratch0, inline_value_class_mask(
                                                InlineValueClass::SMI));
                    return;
                case InlineValueClass::Boolean:
                    {
                        uint64_t expected = inline_value_class_expected_bits(
                            InlineValueClass::Boolean);
                        assembler.emit_logical_imm(LogicalOp::Eor, Scratch0,
                                                   lhs, expected);
                        assembler.emit_logical_imm(LogicalOp::Eor, Scratch1,
                                                   rhs, expected);
                        assembler.emit_logical_reg(LogicalOp::Orr, Scratch0,
                                                   Scratch0, Scratch1);
                        assembler.tst(Scratch0, inline_value_class_mask(
                                                    InlineValueClass::Boolean));
                        return;
                    }
                case InlineValueClass::SMIOrBoolean:
                    assembler.emit_logical_reg(LogicalOp::Orr, Scratch0, lhs,
                                               rhs);
                    assembler.mov(Scratch1,
                                  inline_value_class_mask(
                                      InlineValueClass::SMIOrBoolean));
                    assembler.tst(Scratch0, Scratch1);
                    return;
            }
            assert(false);
        }

        void emit_branch_from_flags(AArch64MacroAssembler &assembler,
                                    AArch64Condition true_condition,
                                    const Block *true_target,
                                    const Block *false_target,
                                    const Block *next_block,
                                    const BlockLabels &block_labels)
        {
            if(true_target == false_target)
            {
                if(true_target != next_block)
                {
                    assembler.b(block_label(block_labels, true_target));
                }
                return;
            }
            if(true_target == next_block)
            {
                assembler.b(invert_condition(true_condition),
                            block_label(block_labels, false_target));
                return;
            }
            assembler.b(true_condition, block_label(block_labels, true_target));
            if(false_target != next_block)
            {
                assembler.b(block_label(block_labels, false_target));
            }
        }

        bool edge_uses_value(const BlockEdge &edge, InstructionId value)
        {
            for(ProgramValueRef argument: edge.arguments())
            {
                if(argument.instruction_id() == value)
                {
                    return true;
                }
            }
            return false;
        }

        bool branch_edges_use_value(ConditionalBranchInstruction branch,
                                    InstructionId value)
        {
            return edge_uses_value(*branch.true_edge(), value) ||
                   edge_uses_value(*branch.false_edge(), value);
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
        SideExitLabels side_exit_labels;
        auto side_exit_target = [&](SideExitBinding binding) {
            auto found = side_exit_labels.find(binding);
            if(found != side_exit_labels.end())
            {
                return pending_side_exits[found->second].label;
            }

            Label label = assembler.emitter().make_label();
            size_t index = pending_side_exits.size();
            pending_side_exits.push_back({label, binding});
            side_exit_labels.emplace(binding, index);
            return label;
        };

        auto emit_single_instruction = [&](Instruction instruction,
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

                case MachineInstructionKind::AndSMI:
                case MachineInstructionKind::OrrSMI:
                case MachineInstructionKind::EorSMI:
                {
                    emit_binary_logical_smi(
                        assembler, locations,
                        instruction.as<BinaryLogicalSMIInstruction>());
                    break;
                }

                case MachineInstructionKind::Is:
                case MachineInstructionKind::IsNot:
                {
                    IsComparisonInstruction comparison =
                        instruction.as<IsComparisonInstruction>();
                    AArch64Condition true_condition = emit_is_comparison(
                        assembler, locations, comparison);
                    emit_tagged_boolean_from_flags(
                        assembler, locations, true_condition, instruction);
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
                    assembler.ldr(
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        AArch64ManagedFramePointerRegister,
                        stack_byte_offset(assigned_stack(
                            locations, load_instruction.source())));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    LoadStackPointerInstruction, load_pointer_instruction)
                {
                    assembler.ldr(
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        AArch64ManagedFramePointerRegister,
                        stack_byte_offset(assigned_stack(
                            locations,
                            load_pointer_instruction.source())));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    StoreStackInstruction, store_instruction)
                {
                    assembler.str(
                        assigned_register(locations,
                                          store_instruction.source()),
                        AArch64ManagedFramePointerRegister,
                        stack_byte_offset(assigned_stack(
                            locations, ProgramValueRef(instruction))));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    StoreStackPointerInstruction,
                    store_pointer_instruction)
                {
                    assembler.str(
                        assigned_register(
                            locations,
                            store_pointer_instruction.source()),
                        AArch64ManagedFramePointerRegister,
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
                        assigned_register(locations, add_instruction.lhs()),
                        assigned_register(locations, add_instruction.rhs()));
                    assembler.b(
                        AArch64Condition::Overflow,
                        side_exit_target(
                            make_side_exit_binding(add_instruction)));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    SubSMIWithSideExitInstruction, sub_instruction)
                {
                    assembler.emit_arithmetic_reg(
                        ArithmeticOp::Subs,
                        assigned_register(locations,
                                          ProgramValueRef(instruction)),
                        assigned_register(locations, sub_instruction.lhs()),
                        assigned_register(locations, sub_instruction.rhs()));
                    assembler.b(
                        AArch64Condition::Overflow,
                        side_exit_target(
                            make_side_exit_binding(sub_instruction)));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    MulSMIWithSideExitInstruction, mul_instruction)
                {
                    XRegister result = assigned_register(
                        locations, ProgramValueRef(mul_instruction));
                    XRegister lhs =
                        assigned_register(locations, mul_instruction.lhs());
                    XRegister temporary =
                        assigned_temporary(locations, mul_instruction, 0);

                    assembler.asr(temporary,
                                  assigned_register(locations,
                                                    mul_instruction.rhs()),
                                  5);
                    assembler.mul(result, lhs, temporary);
                    assembler.smulh(temporary, lhs, temporary);
                    assembler.emit_arithmetic_reg(
                        ArithmeticOp::Subs, xzr, temporary, result,
                        ArithmeticShift::Asr, 63);
                    assembler.b(
                        AArch64Condition::NotEqual,
                        side_exit_target(
                            make_side_exit_binding(mul_instruction)));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    InlineTagGuardWithSideExitInstruction,
                    guard_instruction)
                {
                    XRegister input =
                        assigned_register(locations, guard_instruction.value());
                    emit_inline_tag_test(
                        assembler, input,
                        guard_instruction.expected_class());
                    Label target = side_exit_target(
                        make_side_exit_binding(guard_instruction));
                    assembler.b(AArch64Condition::NotEqual, target);
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    ConditionalBranchInstruction, branch_instruction)
                {
                    const Block *true_target =
                        branch_instruction.true_edge()->target();
                    const Block *false_target =
                        branch_instruction.false_edge()->target();
                    if(true_target != false_target)
                    {
                        XRegister condition = assigned_register(
                            locations, branch_instruction.condition());
                        assembler.tst(condition, value_truthy_mask);
                    }
                    emit_branch_from_flags(
                        assembler, AArch64Condition::NotEqual, true_target,
                        false_target, next_block, block_labels);
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
                        make_side_exit_binding(resume_instruction)));
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    ReturnInstruction, return_instruction)
                {
                    assert(assigned_register(
                               locations, return_instruction.return_value())
                               .encoding() == 0);
                    assert(assigned_register(
                               locations,
                               return_instruction.return_code_object())
                               .encoding() ==
                           AArch64CodeObjectRegister.encoding());
                    assert(assigned_register(locations,
                                             return_instruction.return_pc())
                               .encoding() ==
                           AArch64InterpreterPcRegister.encoding());

                    PhysicalLocation previous_frame_pointer =
                        locations.location_for(
                            return_instruction.previous_frame_pointer());
                    if(previous_frame_pointer.is_register())
                    {
                        XRegister source = assigned_register(
                            locations,
                            return_instruction.previous_frame_pointer());
                        if(source.encoding() !=
                           AArch64ManagedFramePointerRegister.encoding())
                        {
                            assembler.mov(AArch64ManagedFramePointerRegister,
                                          source);
                        }
                    }
                    else
                    {
                        assembler.ldr(
                            AArch64ManagedFramePointerRegister,
                            AArch64ManagedFramePointerRegister,
                            stack_byte_offset(previous_frame_pointer.stack()));
                    }
                    assembler.emit_ret();
                    break;
                }

                case CL_JIT_MACHINE_INSTRUCTION_CASE(
                    BareReturnInstruction, return_instruction)
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

        auto emit_instruction = [&](Instruction instruction,
                                    std::optional<Instruction> next_instruction,
                                    const Block *next_block) {
            if(next_instruction.has_value() &&
               next_instruction->kind() == InstructionKind::ConditionalBranch)
            {
                ConditionalBranchInstruction branch =
                    next_instruction->as<ConditionalBranchInstruction>();
                if(branch.condition().instruction_id() == instruction.id())
                {
                    if(IsComparisonInstruction::accepts_kind(
                           instruction.kind()))
                    {
                        IsComparisonInstruction comparison =
                            instruction.as<IsComparisonInstruction>();
                        AArch64Condition true_condition = emit_is_comparison(
                            assembler, locations, comparison);
                        if(branch_edges_use_value(branch, instruction.id()))
                        {
                            emit_tagged_boolean_from_flags(assembler, locations,
                                                           true_condition,
                                                           instruction);
                        }
                        emit_branch_from_flags(assembler, true_condition,
                                               branch.true_edge()->target(),
                                               branch.false_edge()->target(),
                                               next_block, block_labels);
                        return ConsumedInstructionCount::Two;
                    }
                }
            }

            if(instruction.kind() ==
                   InstructionKind::InlineTagGuardWithSideExit &&
               next_instruction.has_value() &&
               next_instruction->kind() ==
                   InstructionKind::InlineTagGuardWithSideExit)
            {
                InlineTagGuardWithSideExitInstruction guard =
                    instruction.as<InlineTagGuardWithSideExitInstruction>();
                InlineTagGuardWithSideExitInstruction next_guard =
                    next_instruction
                        ->as<InlineTagGuardWithSideExitInstruction>();
                if(guard.expected_class() == next_guard.expected_class() &&
                   make_side_exit_binding(guard) ==
                       make_side_exit_binding(next_guard))
                {
                    emit_combined_inline_tag_test(
                        assembler, assigned_register(locations, guard.value()),
                        assigned_register(locations, next_guard.value()),
                        guard.expected_class());
                    assembler.b(
                        AArch64Condition::NotEqual,
                        side_exit_target(make_side_exit_binding(guard)));
                    return ConsumedInstructionCount::Two;
                }
            }

            emit_single_instruction(instruction, next_block);
            return ConsumedInstructionCount::One;
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
            InstructionRange instructions = block->instructions();
            for(size_t instruction_index = 0;
                instruction_index < instructions.size();)
            {
                Instruction instruction =
                    block->instruction_at(instruction_index);
                std::optional<Instruction> next_instruction;
                if(instruction_index + 1 < instructions.size())
                {
                    next_instruction =
                        block->instruction_at(instruction_index + 1);
                }
                instruction_index += static_cast<size_t>(emit_instruction(
                    instruction, next_instruction, next_block));
            }
        }

        if(!pending_side_exits.empty())
        {
            assert(graph.bytecode_state_order().has_value());
        }
        for(const PendingSideExit &pending: pending_side_exits)
        {
            assembler.emitter().resolve(pending.label);
            std::vector<TransitionInstruction> program =
                emit_aarch64_bound_side_exit_transition_program(
                    *graph.storage(), *graph.bytecode_state_order(),
                    pending.binding, locations);
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
