#include "builtin_types/str.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/graph_rewriter.h"
#include "jit/instruction_traversal.h"
#include "jit/use_lists.h"
#include "object_model/value.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    TEST(JitInstructionTraversal, WalksBodyInstructionsInProgramOrder)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        builder.emplace_parameter<ParameterInstruction>(entry);
        ConstInstruction *condition =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        UnconditionalBranchInstruction *branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ConstInstruction *result =
            builder.emplace_instruction<ConstInstruction>(exit, Value::None());
        ReturnInstruction *return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                exit, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<std::pair<const Block *, const Instruction *>> visited;
        walk_instructions(*graph,
                          InstructionTraversal().with_block_order(
                              BlockWalkOrder::ProgramOrder),
                          [&](const GraphQueries &queries, const Block &block,
                              const Instruction &instruction) {
                              EXPECT_EQ(graph, &queries.graph());
                              visited.emplace_back(&block, &instruction);
                          });

        ASSERT_EQ(4u, visited.size());
        EXPECT_EQ(std::make_pair(static_cast<const Block *>(entry),
                                 static_cast<const Instruction *>(condition)),
                  visited[0]);
        EXPECT_EQ(std::make_pair(static_cast<const Block *>(entry),
                                 static_cast<const Instruction *>(branch)),
                  visited[1]);
        EXPECT_EQ(std::make_pair(static_cast<const Block *>(exit),
                                 static_cast<const Instruction *>(result)),
                  visited[2]);
        EXPECT_EQ(std::make_pair(
                      static_cast<const Block *>(exit),
                      static_cast<const Instruction *>(return_instruction)),
                  visited[3]);
    }

    TEST(JitUseLists, RecordsUseOccurrencesAndZeroUseDefinitions)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterF64Instruction *unused_parameter =
            builder.emplace_parameter<ParameterF64Instruction>(entry);

        std::array<ProgramValueRef, 1> captured_values = {
            ProgramValueRef(parameter)};
        SnapshotInstruction *snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>(captured_values),
                BytecodePC{17});
        AddSMIInstruction *add = builder.emplace_instruction<AddSMIInstruction>(
            entry, TaggedValueRef(parameter), TaggedValueRef(parameter),
            SnapshotRef(snapshot));
        ReturnInstruction *return_instruction =
            builder.emplace_instruction<ReturnInstruction>(entry,
                                                           TaggedValueRef(add));
        ControlFlowGraph *graph = builder.finalize();

        GraphQueries queries = graph->prepare_queries(GraphQuery::Uses);

        const Uses &parameter_uses = queries.uses_of(*parameter);
        EXPECT_EQ(parameter, parameter_uses.def());
        EXPECT_EQ(entry, parameter_uses.block());
        EXPECT_EQ(ResultClass::ProgramValue, parameter_uses.result_class());
        EXPECT_EQ(ValueRepresentation::TaggedValue,
                  parameter_uses.value_representation());
        EXPECT_EQ(3u, parameter_uses.n_uses());
        EXPECT_EQ(3u, parameter_uses.n_instruction_uses());
        EXPECT_EQ(0u, parameter_uses.n_block_argument_uses());
        EXPECT_TRUE(parameter_uses.block_argument_uses().empty());

        const std::vector<InstructionUse> &instruction_uses =
            parameter_uses.instruction_uses();
        ASSERT_EQ(3u, instruction_uses.size());
        EXPECT_EQ(snapshot, instruction_uses[0].instruction);
        EXPECT_EQ(0u, instruction_uses[0].operand_index);
        EXPECT_EQ(add, instruction_uses[1].instruction);
        EXPECT_EQ(0u, instruction_uses[1].operand_index);
        EXPECT_EQ(add, instruction_uses[2].instruction);
        EXPECT_EQ(1u, instruction_uses[2].operand_index);

        const Uses &snapshot_uses = queries.uses_of(*snapshot);
        EXPECT_EQ(ResultClass::Snapshot, snapshot_uses.result_class());
        EXPECT_EQ(ValueRepresentation::None,
                  snapshot_uses.value_representation());
        ASSERT_EQ(1u, snapshot_uses.n_instruction_uses());
        EXPECT_EQ(add, snapshot_uses.instruction_uses()[0].instruction);
        EXPECT_EQ(2u, snapshot_uses.instruction_uses()[0].operand_index);

        const Uses &add_uses = queries.uses_of(*add);
        ASSERT_EQ(1u, add_uses.n_uses());
        EXPECT_EQ(return_instruction,
                  add_uses.instruction_uses()[0].instruction);
        EXPECT_EQ(0u, add_uses.instruction_uses()[0].operand_index);

        const Uses &unused_uses = queries.uses_of(*unused_parameter);
        EXPECT_EQ(ValueRepresentation::F64, unused_uses.value_representation());
        EXPECT_EQ(0u, unused_uses.n_uses());
        EXPECT_TRUE(unused_uses.instruction_uses().empty());
        EXPECT_TRUE(unused_uses.block_argument_uses().empty());

        GraphQueries reused_queries = graph->prepare_queries(GraphQuery::Uses);
        EXPECT_EQ(&parameter_uses, &reused_queries.uses_of(*parameter));
    }

    TEST(JitUseLists, TraversalPreparesOnlyRequestedQueries)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ConstInstruction *constant =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(constant));
        ControlFlowGraph *graph = builder.finalize();

        size_t visited = 0;
        walk_instructions(
            *graph, InstructionTraversal().with_queries(GraphQuery::Uses),
            [&](const GraphQueries &queries, const Block &,
                const Instruction &) {
                EXPECT_EQ(1u, queries.uses_of(*constant).n_uses());
                ++visited;
            });
        EXPECT_EQ(2u, visited);

        GraphQueries no_queries = graph->prepare_queries(GraphQuery::None);
        EXPECT_DEATH((void)no_queries.uses_of(*constant),
                     "without being requested");
    }

    TEST(JitGraphRewriter, KeepsAnUnchangedGraphWithoutAdvancingGeneration)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ConstInstruction *constant =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        ReturnInstruction *return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(constant));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [](RewriteContext &, const GraphQueries &, const Block &,
               const Instruction &) { return RewriteResult::keep(); });

        EXPECT_FALSE(summary.instructions_changed);
        EXPECT_FALSE(summary.terminators_changed);
        EXPECT_EQ(0u, graph->mutation_generation());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(constant, entry->instructions()[0]);
        EXPECT_EQ(return_instruction, entry->instructions()[1]);
    }

    TEST(JitGraphRewriter, RewriteContextRetainsNewManagedValues)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());
        String *string =
            context.thread()->make_internal_raw<String>(L"folded constant");
        TValue<String> value = TValue<String>::from_oop(string);

        EXPECT_EQ(0, string->refcount);
        {
            CompilationSession session;
            GraphBuilder builder(session);
            Block *entry = builder.emplace_block();
            ConstInstruction *constant =
                builder.emplace_instruction<ConstInstruction>(entry,
                                                              Value::None());
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(constant));
            ControlFlowGraph *graph = builder.finalize();

            GraphRewriter rewriter(session, *graph);
            ConstInstruction *folded_constant = nullptr;
            RewriteSummary summary = rewriter.rewrite_instructions(
                InstructionTraversal(),
                [&](RewriteContext &rewrite_context, const GraphQueries &,
                    const Block &, const Instruction &instruction) {
                    if(instruction.kind() == InstructionKind::Const)
                    {
                        TValue<String> retained =
                            rewrite_context.retain_and_pin_value(value);
                        folded_constant =
                            rewrite_context.make_instruction<ConstInstruction>(
                                retained.raw_value());
                        return RewriteResult::replace(folded_constant);
                    }
                    return RewriteResult::keep();
                });

            EXPECT_TRUE(summary.instructions_changed);
            ASSERT_NE(nullptr, folded_constant);
            EXPECT_EQ(value.raw_value(), folded_constant->constant());
            EXPECT_EQ(folded_constant, entry->instructions()[0]);
            EXPECT_EQ(1, string->refcount);
        }
        EXPECT_EQ(0, string->refcount);
    }

    TEST(JitGraphRewriter,
         InsertsPrefixesAndSuffixesAroundTheCurrentInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction *first = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        MovInstruction *second = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(first));
        ReturnInstruction *return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(second));
        ControlFlowGraph *graph = builder.finalize();

        MovInstruction *prefix = nullptr;
        MovInstruction *suffix = nullptr;
        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &context, const GraphQueries &, const Block &,
                const Instruction &instruction) {
                if(&instruction == first)
                {
                    prefix = context.make_instruction<MovInstruction>(
                        TaggedValueRef(parameter));
                    return RewriteResult::keep_with_prefix({prefix});
                }
                if(&instruction == second)
                {
                    suffix = context.make_instruction<MovInstruction>(
                        TaggedValueRef(second));
                    return RewriteResult::keep_with_suffix({suffix});
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_FALSE(summary.terminators_changed);
        EXPECT_EQ(1u, graph->mutation_generation());
        EXPECT_FALSE(first->is_detached());
        EXPECT_FALSE(second->is_detached());
        EXPECT_FALSE(return_instruction->is_detached());
        ASSERT_EQ(5u, entry->instructions().size());
        EXPECT_EQ(prefix, entry->instructions()[0]);
        EXPECT_EQ(first, entry->instructions()[1]);
        EXPECT_EQ(second, entry->instructions()[2]);
        EXPECT_EQ(suffix, entry->instructions()[3]);
        EXPECT_EQ(return_instruction, entry->instructions()[4]);
        EXPECT_EQ(second, suffix->source().instruction());
    }

    TEST(JitGraphRewriter, ReplacesAnIdentityWithItsExistingDefinition)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction *move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        ReturnInstruction *old_return =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(move));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal().with_queries(GraphQuery::Uses),
            [&](RewriteContext &, const GraphQueries &queries, const Block &,
                const Instruction &instruction) {
                if(instruction.kind() == InstructionKind::Mov)
                {
                    EXPECT_EQ(1u, queries.uses_of(instruction).n_uses());
                    return RewriteResult::replace_with_def(
                        instruction.as<MovInstruction>()->source());
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_EQ(1u, graph->mutation_generation());
        EXPECT_TRUE(move->is_detached());
        EXPECT_TRUE(old_return->is_detached());
        EXPECT_DEATH((void)old_return->kind(), "detached JIT instruction");
        ASSERT_EQ(1u, entry->instructions().size());
        const ReturnInstruction *new_return =
            entry->instructions()[0]->as<ReturnInstruction>();
        EXPECT_EQ(parameter, new_return->return_value().instruction());

        GraphQueries rebuilt_queries = graph->prepare_queries(GraphQuery::Uses);
        EXPECT_EQ(1u, rebuilt_queries.uses_of(*parameter).n_uses());
    }

    TEST(JitGraphRewriter, CanPassNormalizedInstructionsToTheCallback)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ConstInstruction *old_constant =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        MovInstruction *old_move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(old_constant));
        ReturnInstruction *old_return =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(old_move));
        ControlFlowGraph *graph = builder.finalize();

        ConstInstruction *new_constant = nullptr;
        MovInstruction *return_prefix = nullptr;
        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(), RewriteInput::Normalized,
            [&](RewriteContext &context, const GraphQueries &, const Block &,
                const Instruction &instruction) {
                switch(instruction.kind())
                {
                    case InstructionKind::Const:
                        new_constant =
                            context.make_instruction<ConstInstruction>(
                                Value::True());
                        return RewriteResult::replace(new_constant);
                    case InstructionKind::Mov:
                        EXPECT_NE(old_move, &instruction);
                        EXPECT_EQ(new_constant,
                                  instruction.as<MovInstruction>()
                                      ->source()
                                      .instruction());
                        return RewriteResult::replace_with_def(
                            instruction.as<MovInstruction>()->source());
                    case InstructionKind::Return:
                        EXPECT_NE(old_return, &instruction);
                        EXPECT_EQ(new_constant,
                                  instruction.as<ReturnInstruction>()
                                      ->return_value()
                                      .instruction());
                        return_prefix =
                            context.make_instruction<MovInstruction>(
                                TaggedValueRef(new_constant));
                        return RewriteResult::keep_with_prefix({return_prefix});
                    default:
                        break;
                }
                ADD_FAILURE() << "unexpected instruction kind";
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_TRUE(old_constant->is_detached());
        EXPECT_TRUE(old_move->is_detached());
        EXPECT_TRUE(old_return->is_detached());
        ASSERT_EQ(3u, entry->instructions().size());
        EXPECT_EQ(new_constant, entry->instructions()[0]);
        EXPECT_EQ(return_prefix, entry->instructions()[1]);
        EXPECT_EQ(new_constant, entry->instructions()[2]
                                    ->as<ReturnInstruction>()
                                    ->return_value()
                                    .instruction());
    }

    TEST(JitGraphRewriter, RejectsUseListsWithNormalizedInput)
    {
        EXPECT_DEATH(
            {
                CompilationSession session;
                GraphBuilder builder(session);
                Block *entry = builder.emplace_block();
                ConstInstruction *constant =
                    builder.emplace_instruction<ConstInstruction>(
                        entry, Value::None());
                builder.emplace_instruction<ReturnInstruction>(
                    entry, TaggedValueRef(constant));
                ControlFlowGraph *graph = builder.finalize();

                GraphRewriter rewriter(session, *graph);
                rewriter.rewrite_instructions(
                    InstructionTraversal().with_queries(GraphQuery::Uses),
                    RewriteInput::Normalized,
                    [](RewriteContext &, const GraphQueries &, const Block &,
                       const Instruction &) { return RewriteResult::keep(); });
            },
            "normalized rewrite input");
    }

    TEST(JitGraphRewriter, ReconstructsVariadicInstructionsFromTheSchema)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ConstInstruction *callable =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(callable)};
        SnapshotInstruction *snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>(captured),
                BytecodePC{31});
        std::array<TaggedValueRef, 2> arguments = {TaggedValueRef(parameter),
                                                   TaggedValueRef(callable)};
        PythonCallInstruction *call =
            builder.emplace_instruction<PythonCallInstruction>(
                entry, TaggedValueRef(callable), SnapshotRef(snapshot),
                std::span<const TaggedValueRef>(arguments), BytecodePC{47});
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(call));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &context, const GraphQueries &, const Block &,
                const Instruction &instruction) {
                if(&instruction == callable)
                {
                    return RewriteResult::replace(
                        context.make_instruction<ConstInstruction>(
                            Value::True()));
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(callable->is_detached());
        EXPECT_TRUE(snapshot->is_detached());
        EXPECT_TRUE(call->is_detached());
        ASSERT_EQ(4u, entry->instructions().size());
        const auto *new_callable =
            entry->instructions()[0]->as<ConstInstruction>();
        const auto *new_snapshot =
            entry->instructions()[1]->as<SnapshotInstruction>();
        const auto *new_call =
            entry->instructions()[2]->as<PythonCallInstruction>();
        EXPECT_EQ(Value::True(), new_callable->constant());
        EXPECT_EQ(new_callable,
                  new_snapshot->captured_values()[0].instruction());
        EXPECT_EQ(new_callable, new_call->callable().instruction());
        ASSERT_EQ(2u, new_call->arguments().size());
        EXPECT_EQ(parameter, new_call->arguments()[0].instruction());
        EXPECT_EQ(new_callable, new_call->arguments()[1].instruction());
        EXPECT_EQ(new_snapshot, new_call->snapshot().instruction());
        EXPECT_EQ(BytecodePC{47}, new_call->interpreter_return_pc());
    }

    TEST(JitGraphRewriter, StagesSequencesAcrossTheWholeGraphBeforeCommit)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction *move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        UnconditionalBranchInstruction *branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ConstInstruction *constant =
            builder.emplace_instruction<ConstInstruction>(exit, Value::None());
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(constant));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &context, const GraphQueries &,
                const Block &block, const Instruction &instruction) {
                if(&instruction == move)
                {
                    MovInstruction *first =
                        context.make_instruction<MovInstruction>(
                            TaggedValueRef(parameter));
                    MovInstruction *second =
                        context.make_instruction<MovInstruction>(
                            TaggedValueRef(first));
                    return RewriteResult::replace({first, second},
                                                  TaggedValueRef(second));
                }
                if(block.serial() == exit->serial() &&
                   instruction.kind() == InstructionKind::Const)
                {
                    EXPECT_EQ(2u, entry->instructions().size());
                    EXPECT_EQ(move, entry->instructions()[0]);
                    EXPECT_EQ(branch, entry->instructions()[1]);
                    EXPECT_FALSE(move->is_detached());
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_FALSE(summary.terminators_changed);
        EXPECT_TRUE(move->is_detached());
        ASSERT_EQ(3u, entry->instructions().size());
        const auto *first = entry->instructions()[0]->as<MovInstruction>();
        const auto *second = entry->instructions()[1]->as<MovInstruction>();
        EXPECT_EQ(parameter, first->source().instruction());
        EXPECT_EQ(first, second->source().instruction());
        EXPECT_EQ(branch, entry->instructions()[2]);
    }

    TEST(JitGraphRewriter, RejectsAUseOfAnErasedDefinition)
    {
        EXPECT_DEATH(
            {
                CompilationSession session;
                GraphBuilder builder(session);
                Block *entry = builder.emplace_block();
                ConstInstruction *constant =
                    builder.emplace_instruction<ConstInstruction>(
                        entry, Value::None());
                builder.emplace_instruction<ReturnInstruction>(
                    entry, TaggedValueRef(constant));
                ControlFlowGraph *graph = builder.finalize();

                GraphRewriter rewriter(session, *graph);
                rewriter.rewrite_instructions(
                    InstructionTraversal(),
                    [&](RewriteContext &, const GraphQueries &, const Block &,
                        const Instruction &instruction) {
                        if(&instruction == constant)
                        {
                            return RewriteResult::erase();
                        }
                        return RewriteResult::keep();
                    });
            },
            "erased definition");
    }

}  // namespace cl::jit
