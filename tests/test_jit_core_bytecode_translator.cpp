#include "jit/core_bytecode_translator.h"

#include "bytecode/code_object_builder.h"
#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct TranslatorFixture
        {
            TranslatorFixture()
                : activation_scope(context.thread()),
                  name(context.vm().get_or_create_interned_string_value(
                      L"<jit-core-translation-test>")),
                  code_builder(&context.vm(), nullptr,
                               TValue<ModuleObject>::from_oop(
                                   context.make_test_module_object(
                                       name, context.vm()
                                                 .global_builtins_module()
                                                 .raw_value())),
                               nullptr, name),
                  graph_builder(session)
            {
            }

            ControlFlowGraph *translate()
            {
                CodeObject *code_object = code_builder.finalize().value();
                CoreBytecodeTranslator translator(*code_object, graph_builder);
                return translator.translate();
            }

            test::VmTestContext context;
            ThreadState::ActivationScope activation_scope;
            TValue<String> name;
            CodeObjectBuilder code_builder;
            CompilationSession session;
            GraphBuilder graph_builder;
        };

        std::vector<Instruction *> instructions_of_kind(const Block *block,
                                                        InstructionKind kind)
        {
            std::vector<Instruction *> matches;
            for(Instruction *instruction: block->instructions())
            {
                if(instruction->kind() == kind)
                {
                    matches.push_back(instruction);
                }
            }
            return matches;
        }

        size_t bytecode_state_size(const ControlFlowGraph &graph)
        {
            EXPECT_TRUE(graph.bytecode_state_order().has_value());
            return graph.bytecode_state_order()->size();
        }
    }  // namespace

    TEST(JitCoreBytecodeTranslator,
         ConstantsAndRegisterTransfersPreserveProgramValueIdentity)
    {
        TranslatorFixture fixture;
        uint32_t constant_index =
            fixture.code_builder.allocate_constant(fixture.name.raw_value())
                .value();
        {
            CodeObjectBuilder::TemporaryReg temporaries(fixture.code_builder,
                                                        2);
            fixture.code_builder.emit_lda_smi(0, 37).value();
            fixture.code_builder.emit_star(0, temporaries).value();
            fixture.code_builder.emit_lda_none(0).value();
            fixture.code_builder.emit_lda_constant(0, uint8_t(constant_index))
                .value();
            fixture.code_builder
                .emit_mov(0, uint32_t(temporaries) + 1, temporaries)
                .value();
            fixture.code_builder.emit_ldar(0, uint32_t(temporaries) + 1)
                .value();
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(1u, graph->blocks().size());
        Block *entry = graph->entry_block();

        std::vector<Instruction *> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(3u, constants.size());
        EXPECT_EQ(Value::from_smi(37),
                  constants[0]->as<ConstInstruction>()->constant());
        EXPECT_EQ(Value::None(),
                  constants[1]->as<ConstInstruction>()->constant());
        EXPECT_EQ(fixture.name.raw_value(),
                  constants[2]->as<ConstInstruction>()->constant());
        EXPECT_TRUE(instructions_of_kind(entry, InstructionKind::Mov).empty());

        ReturnInstruction *return_instruction =
            entry->instructions().back()->as<ReturnInstruction>();
        EXPECT_EQ(constants[0]->id(),
                  return_instruction->return_value().instruction_id());
    }

    TEST(JitCoreBytecodeTranslator, TranslatesIdentityTests)
    {
        TranslatorFixture fixture;
        {
            CodeObjectBuilder::TemporaryReg temporaries(fixture.code_builder,
                                                        2);
            fixture.code_builder.emit_lda_true(0).value();
            fixture.code_builder.emit_star(0, temporaries).value();
            fixture.code_builder.emit_lda_false(0).value();
            fixture.code_builder.emit_star(0, uint32_t(temporaries) + 1)
                .value();
            fixture.code_builder
                .emit_operator_reg(0, Bytecode::TestIs, temporaries,
                                   OperatorBytecodeFormat::Plain)
                .value();
            fixture.code_builder.emit_ldar(0, uint32_t(temporaries) + 1)
                .value();
            fixture.code_builder
                .emit_operator_reg(0, Bytecode::TestIsNot, temporaries,
                                   OperatorBytecodeFormat::Plain)
                .value();
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction *> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        std::vector<Instruction *> is_instructions =
            instructions_of_kind(entry, InstructionKind::Is);
        std::vector<Instruction *> is_not_instructions =
            instructions_of_kind(entry, InstructionKind::IsNot);
        ASSERT_EQ(2u, constants.size());
        ASSERT_EQ(1u, is_instructions.size());
        ASSERT_EQ(1u, is_not_instructions.size());

        IsInstruction *is = is_instructions.front()->as<IsInstruction>();
        EXPECT_EQ(constants[0]->id(), is->lhs().instruction_id());
        EXPECT_EQ(constants[1]->id(), is->rhs().instruction_id());
        IsNotInstruction *is_not =
            is_not_instructions.front()->as<IsNotInstruction>();
        EXPECT_EQ(constants[0]->id(), is_not->lhs().instruction_id());
        EXPECT_EQ(constants[1]->id(), is_not->rhs().instruction_id());
    }

    TEST(JitCoreBytecodeTranslator,
         JumpIfFalseMapsFallthroughToTrueAndJumpToFalse)
    {
        TranslatorFixture fixture;
        JumpTarget jump_target(&fixture.code_builder);
        fixture.code_builder.emit_lda_true(0).value();
        fixture.code_builder.emit_jump_if_false(0, jump_target).value();
        fixture.code_builder.emit_lda_smi(0, 1).value();
        fixture.code_builder.emit_return(0).value();
        jump_target.resolve().value();
        fixture.code_builder.emit_lda_smi(0, 2).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(3u, graph->blocks().size());
        Block *entry = graph->blocks()[0];
        Block *fallthrough = graph->blocks()[1];
        Block *jump = graph->blocks()[2];
        ConditionalBranchInstruction *branch =
            entry->instructions().back()->as<ConditionalBranchInstruction>();

        EXPECT_EQ(fallthrough, branch->true_edge()->target());
        EXPECT_EQ(jump, branch->false_edge()->target());
        size_t state_size = bytecode_state_size(*graph);
        ASSERT_EQ(state_size, branch->true_edge()->arguments().size());
        ASSERT_EQ(state_size, branch->false_edge()->arguments().size());
        EXPECT_EQ(branch->condition().instruction_id(),
                  branch->true_edge()->arguments()[0].instruction_id());
        EXPECT_EQ(branch->condition().instruction_id(),
                  branch->false_edge()->arguments()[0].instruction_id());
        EXPECT_EQ(state_size, fallthrough->parameters().size());
        EXPECT_EQ(state_size, jump->parameters().size());
    }

    TEST(JitCoreBytecodeTranslator,
         JumpIfTrueMapsJumpToTrueAndFallthroughToFalse)
    {
        TranslatorFixture fixture;
        JumpTarget jump_target(&fixture.code_builder);
        fixture.code_builder.emit_lda_false(0).value();
        fixture.code_builder.emit_jump_if_true(0, jump_target).value();
        fixture.code_builder.emit_lda_smi(0, 1).value();
        fixture.code_builder.emit_return(0).value();
        jump_target.resolve().value();
        fixture.code_builder.emit_lda_smi(0, 2).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(3u, graph->blocks().size());
        ConditionalBranchInstruction *branch =
            graph->blocks()[0]
                ->instructions()
                .back()
                ->as<ConditionalBranchInstruction>();

        EXPECT_EQ(graph->blocks()[2], branch->true_edge()->target());
        EXPECT_EQ(graph->blocks()[1], branch->false_edge()->target());
    }

    TEST(JitCoreBytecodeTranslator,
         SequentialBoundaryAndLoopBackedgeCarryCompleteState)
    {
        TranslatorFixture fixture;
        fixture.code_builder.emit_lda_smi(0, 0).value();
        JumpTarget loop(&fixture.code_builder);
        loop.resolve().value();
        fixture.code_builder.emit_lda_true(0).value();
        fixture.code_builder.emit_jump_if_true(0, loop).value();
        fixture.code_builder.emit_lda_none(0).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(3u, graph->blocks().size());
        Block *entry = graph->blocks()[0];
        Block *loop_block = graph->blocks()[1];

        UnconditionalBranchInstruction *entry_branch =
            entry->instructions().back()->as<UnconditionalBranchInstruction>();
        EXPECT_EQ(loop_block, entry_branch->edge()->target());
        size_t state_size = bytecode_state_size(*graph);
        ASSERT_EQ(state_size, entry_branch->edge()->arguments().size());
        ASSERT_EQ(state_size, loop_block->parameters().size());

        ConditionalBranchInstruction *loop_branch =
            loop_block->instructions()
                .back()
                ->as<ConditionalBranchInstruction>();
        EXPECT_EQ(loop_block, loop_branch->true_edge()->target());
        ASSERT_EQ(state_size, loop_branch->true_edge()->arguments().size());
        ASSERT_EQ(2u, loop_block->predecessor_edges().size());
        EXPECT_EQ(entry_branch->edge(), loop_block->predecessor_edges()[0]);
        EXPECT_EQ(loop_branch->true_edge(), loop_block->predecessor_edges()[1]);
    }

    TEST(JitCoreBytecodeTranslator,
         UnsupportedSequentialInstructionSnapshotsAndPoisonsContinuation)
    {
        TranslatorFixture fixture;
        fixture.code_builder.emit_lda_smi(0, 7).value();
        fixture.code_builder.emit_to_bool(0).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction *> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        std::vector<Instruction *> resumes =
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter);
        std::vector<Instruction *> uninitialized =
            instructions_of_kind(entry, InstructionKind::Uninitialized);
        ASSERT_EQ(1u, snapshots.size());
        ASSERT_EQ(1u, resumes.size());
        ASSERT_EQ(3u, uninitialized.size());

        SnapshotInstruction *snapshot =
            snapshots.front()->as<SnapshotInstruction>();
        EXPECT_EQ(2u, snapshot->resume_pc());
        ASSERT_EQ(bytecode_state_size(*graph),
                  snapshot->captured_values().size());
        std::vector<Instruction *> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(1u, constants.size());
        EXPECT_EQ(constants.front()->id(),
                  snapshot->captured_values()[0].instruction_id());
        EXPECT_EQ(snapshot->id(), resumes.front()
                                      ->as<ResumeInInterpreterInstruction>()
                                      ->snapshot()
                                      .instruction_id());

        ReturnInstruction *return_instruction =
            entry->instructions().back()->as<ReturnInstruction>();
        EXPECT_EQ(uninitialized.back()->id(),
                  return_instruction->return_value().instruction_id());
    }

    TEST(JitCoreBytecodeTranslator,
         EntryParameterAliasesIntoCompleteSnapshotState)
    {
        TranslatorFixture fixture;
        fixture.code_builder.n_parameters() = 1;
        fixture.code_builder.n_positional_parameters() = 1;
        fixture.code_builder.emit_ldar(0, 0).value();
        fixture.code_builder.emit_to_bool(0).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        ASSERT_EQ(1u, entry->parameters().size());
        Instruction *parameter = entry->parameters().front();
        std::vector<Instruction *> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        ASSERT_EQ(1u, snapshots.size());
        SnapshotValueRefRange captured =
            snapshots.front()->as<SnapshotInstruction>()->captured_values();
        ASSERT_EQ(bytecode_state_size(*graph), captured.size());
        EXPECT_EQ(parameter->id(), captured[0].instruction_id());
        EXPECT_EQ(parameter->id(), captured[1].instruction_id());
    }

    TEST(JitCoreBytecodeTranslator,
         UnsupportedConditionalPoisonsOnlyStructuralCondition)
    {
        TranslatorFixture fixture;
        JumpTarget jump_target(&fixture.code_builder);
        fixture.code_builder.emit_lda_smi(0, 7).value();
        fixture.code_builder.emit_jump_if_equal_smi(0, 7, jump_target).value();
        fixture.code_builder.emit_lda_smi(0, 1).value();
        fixture.code_builder.emit_return(0).value();
        jump_target.resolve().value();
        fixture.code_builder.emit_lda_smi(0, 2).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction *> resumes =
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter);
        ASSERT_EQ(1u, resumes.size());
        ConditionalBranchInstruction *branch =
            entry->instructions().back()->as<ConditionalBranchInstruction>();
        EXPECT_EQ(InstructionKind::Uninitialized,
                  graph->storage()
                      ->instruction(branch->condition().instruction_id())
                      ->kind());

        std::vector<Instruction *> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(1u, constants.size());
        size_t state_size = bytecode_state_size(*graph);
        ASSERT_EQ(state_size, branch->true_edge()->arguments().size());
        ASSERT_EQ(state_size, branch->false_edge()->arguments().size());
        EXPECT_EQ(constants.front()->id(),
                  branch->true_edge()->arguments()[0].instruction_id());
        EXPECT_EQ(constants.front()->id(),
                  branch->false_edge()->arguments()[0].instruction_id());
    }

}  // namespace cl::jit
