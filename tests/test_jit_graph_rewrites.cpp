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
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    TEST(JitInstructionTraversal, WalksBodyInstructionsInProgramOrder)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        builder.emplace_parameter<ParameterInstruction>(entry);
        ConstInstruction condition =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        UnconditionalBranchInstruction branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ConstInstruction result =
            builder.emplace_instruction<ConstInstruction>(exit, Value::None());
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                exit, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<std::pair<const Block *, Instruction>> visited;
        walk_instructions(*graph,
                          InstructionTraversal().with_block_order(
                              BlockWalkOrder::ProgramOrder),
                          [&](const GraphQueries &queries, const Block &block,
                              const Instruction &instruction) {
                              EXPECT_EQ(graph, &queries.graph());
                              visited.emplace_back(&block, instruction);
                          });

        ASSERT_EQ(4u, visited.size());
        EXPECT_EQ(std::make_pair(static_cast<const Block *>(entry),
                                 static_cast<Instruction>(condition)),
                  visited[0]);
        EXPECT_EQ(std::make_pair(static_cast<const Block *>(entry),
                                 static_cast<Instruction>(branch)),
                  visited[1]);
        EXPECT_EQ(std::make_pair(static_cast<const Block *>(exit),
                                 static_cast<Instruction>(result)),
                  visited[2]);
        EXPECT_EQ(std::make_pair(static_cast<const Block *>(exit),
                                 static_cast<Instruction>(return_instruction)),
                  visited[3]);
    }

    TEST(JitUseLists, RecordsUseOccurrencesAndZeroUseDefinitions)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterF64Instruction unused_parameter =
            builder.emplace_parameter<ParameterF64Instruction>(entry);

        std::array<ProgramValueRef, 1> captured_values = {
            ProgramValueRef(parameter)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>(captured_values),
                BytecodePC{17});
        AddSMIInstruction add = builder.emplace_instruction<AddSMIInstruction>(
            entry, TaggedValueRef(parameter), TaggedValueRef(parameter),
            SnapshotRef(snapshot));
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(entry,
                                                           TaggedValueRef(add));
        ControlFlowGraph *graph = builder.finalize();

        GraphQueries queries = graph->prepare_queries(GraphQuery::Uses);

        const Uses &parameter_uses = queries.uses_of(parameter);
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
        EXPECT_EQ(snapshot.id(), instruction_uses[0].instruction);
        EXPECT_EQ(0u, instruction_uses[0].operand_index);
        EXPECT_EQ(add.id(), instruction_uses[1].instruction);
        EXPECT_EQ(0u, instruction_uses[1].operand_index);
        EXPECT_EQ(add.id(), instruction_uses[2].instruction);
        EXPECT_EQ(1u, instruction_uses[2].operand_index);

        const Uses &snapshot_uses = queries.uses_of(snapshot);
        EXPECT_EQ(ResultClass::Snapshot, snapshot_uses.result_class());
        EXPECT_EQ(ValueRepresentation::None,
                  snapshot_uses.value_representation());
        ASSERT_EQ(1u, snapshot_uses.n_instruction_uses());
        EXPECT_EQ(add.id(), snapshot_uses.instruction_uses()[0].instruction);
        EXPECT_EQ(2u, snapshot_uses.instruction_uses()[0].operand_index);

        const Uses &add_uses = queries.uses_of(add);
        ASSERT_EQ(1u, add_uses.n_uses());
        EXPECT_EQ(return_instruction.id(),
                  add_uses.instruction_uses()[0].instruction);
        EXPECT_EQ(0u, add_uses.instruction_uses()[0].operand_index);

        const Uses &unused_uses = queries.uses_of(unused_parameter);
        EXPECT_EQ(ValueRepresentation::F64, unused_uses.value_representation());
        EXPECT_EQ(0u, unused_uses.n_uses());
        EXPECT_TRUE(unused_uses.instruction_uses().empty());
        EXPECT_TRUE(unused_uses.block_argument_uses().empty());

        GraphQueries reused_queries = graph->prepare_queries(GraphQuery::Uses);
        EXPECT_EQ(&parameter_uses, &reused_queries.uses_of(parameter));
    }

    TEST(JitUseLists, RecordsBlockArgumentUseOccurrences)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction source_parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {
            ProgramValueRef(source_parameter)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction target_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(target_parameter));
        ControlFlowGraph *graph = builder.finalize();

        const Uses &uses =
            graph->prepare_queries(GraphQuery::Uses).uses_of(source_parameter);

        EXPECT_EQ(1u, uses.n_uses());
        EXPECT_EQ(0u, uses.n_instruction_uses());
        ASSERT_EQ(1u, uses.n_block_argument_uses());
        EXPECT_EQ(edge, uses.block_argument_uses()[0].edge);
        EXPECT_EQ(0u, uses.block_argument_uses()[0].argument_index);
    }

    TEST(JitUseLists, TraversalPreparesOnlyRequestedQueries)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ConstInstruction constant =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(constant));
        ControlFlowGraph *graph = builder.finalize();

        size_t visited = 0;
        walk_instructions(*graph,
                          InstructionTraversal().with_queries(GraphQuery::Uses),
                          [&](const GraphQueries &queries, const Block &,
                              const Instruction &) {
                              EXPECT_EQ(1u, queries.uses_of(constant).n_uses());
                              ++visited;
                          });
        EXPECT_EQ(2u, visited);

        GraphQueries no_queries = graph->prepare_queries(GraphQuery::None);
        EXPECT_DEATH((void)no_queries.uses_of(constant),
                     "without being requested");
    }

    TEST(JitGraphRewriter, KeepsAnUnchangedGraphWithoutAdvancingGeneration)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ConstInstruction constant =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        ReturnInstruction return_instruction =
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
        EXPECT_FALSE(summary.ir_level_changed);
        EXPECT_TRUE(summary.normalization_remapping.empty());
        EXPECT_EQ(0u, graph->mutation_generation());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(constant, entry->instruction_at(0));
        EXPECT_EQ(return_instruction, entry->instruction_at(1));
    }

    TEST(JitGraphRewriter, StagesAnEdgeSplitAfterItsSource)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *target = builder.emplace_block();
        ParameterInstruction tagged =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterF64Instruction floating =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ParameterPointerInstruction pointer =
            builder.emplace_parameter<ParameterPointerInstruction>(entry);
        std::array<ProgramValueRef, 3> arguments = {ProgramValueRef(tagged),
                                                    ProgramValueRef(floating),
                                                    ProgramValueRef(pointer)};
        BlockEdge *edge = builder.make_block_edge(entry, target, arguments);
        UnconditionalBranchInstruction old_branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ParameterInstruction target_tagged =
            builder.emplace_parameter<ParameterInstruction>(target);
        builder.emplace_parameter<ParameterF64Instruction>(target);
        builder.emplace_parameter<ParameterPointerInstruction>(target);
        builder.emplace_instruction<ReturnInstruction>(
            target, TaggedValueRef(target_tagged));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        std::array requests = {
            EdgeSplitRequest{edge, EdgeSplitPlacement::AfterSource}};
        std::vector<Block *> splits = rewriter.stage_edge_splits(requests);
        ASSERT_EQ(1u, splits.size());
        Block *split = splits.front();

        struct Callback
        {
            std::vector<const Block *> entered_blocks;

            RewriteInsertion at_block_entry(RewriteContext &,
                                            const GraphQueries &,
                                            const Block &block)
            {
                entered_blocks.push_back(&block);
                return RewriteInsertion::none();
            }
        } callback;
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), callback);

        EXPECT_TRUE(summary.blocks_changed);
        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_EQ(1u, graph->mutation_generation());
        ASSERT_EQ(3u, graph->blocks().size());
        EXPECT_EQ(entry, graph->blocks()[0]);
        EXPECT_EQ(split, graph->blocks()[1]);
        EXPECT_EQ(target, graph->blocks()[2]);
        ASSERT_EQ(3u, callback.entered_blocks.size());
        EXPECT_EQ(entry, callback.entered_blocks[0]);
        EXPECT_EQ(split, callback.entered_blocks[1]);
        EXPECT_EQ(target, callback.entered_blocks[2]);

        ASSERT_EQ(3u, split->parameters().size());
        EXPECT_EQ(InstructionKind::Parameter, split->parameter_at(0).kind());
        EXPECT_EQ(InstructionKind::ParameterF64, split->parameter_at(1).kind());
        EXPECT_EQ(InstructionKind::ParameterPointer,
                  split->parameter_at(2).kind());

        BlockEdge *incoming = entry->block_successor_edges().front();
        EXPECT_EQ(split, incoming->target());
        EXPECT_EQ(arguments[0], incoming->arguments()[0]);
        EXPECT_EQ(arguments[1], incoming->arguments()[1]);
        EXPECT_EQ(arguments[2], incoming->arguments()[2]);

        BlockEdge *outgoing = split->block_successor_edges().front();
        EXPECT_EQ(target, outgoing->target());
        for(size_t index = 0; index < outgoing->arguments().size(); ++index)
        {
            EXPECT_EQ(split->parameter_at(index).id(),
                      outgoing->arguments()[index].instruction_id());
        }
        EXPECT_TRUE(old_branch.is_poisoned());
        ASSERT_EQ(1u, split->predecessor_edges().size());
        EXPECT_EQ(incoming, split->predecessor_edges().front());
        ASSERT_EQ(1u, target->predecessor_edges().size());
        EXPECT_EQ(outgoing, target->predecessor_edges().front());
    }

    TEST(JitGraphRewriter, StagesAnEdgeSplitBeforeItsTarget)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *other = builder.emplace_block();
        Block *target = builder.emplace_block();
        ParameterInstruction condition =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(condition)};
        BlockEdge *target_edge =
            builder.make_block_edge(entry, target, arguments);
        BlockEdge *other_edge =
            builder.make_block_edge(entry, other, arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(condition), target_edge, other_edge);
        ParameterInstruction other_parameter =
            builder.emplace_parameter<ParameterInstruction>(other);
        std::array<ProgramValueRef, 1> other_arguments = {
            ProgramValueRef(other_parameter)};
        BlockEdge *other_to_target =
            builder.make_block_edge(other, target, other_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            other, other_to_target);
        ParameterInstruction target_parameter =
            builder.emplace_parameter<ParameterInstruction>(target);
        builder.emplace_instruction<ReturnInstruction>(
            target, TaggedValueRef(target_parameter));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        std::array requests = {
            EdgeSplitRequest{target_edge, EdgeSplitPlacement::BeforeTarget}};
        Block *split = rewriter.stage_edge_splits(requests).front();
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [](RewriteContext &, const GraphQueries &, const Block &,
               const Instruction &) { return RewriteResult::keep(); });

        EXPECT_TRUE(summary.blocks_changed);
        ASSERT_EQ(4u, graph->blocks().size());
        EXPECT_EQ(entry, graph->blocks()[0]);
        EXPECT_EQ(other, graph->blocks()[1]);
        EXPECT_EQ(split, graph->blocks()[2]);
        EXPECT_EQ(target, graph->blocks()[3]);
        EXPECT_EQ(split, entry->block_successor_edges()[0]->target());
        EXPECT_EQ(other, entry->block_successor_edges()[1]->target());
        EXPECT_EQ(target, split->block_successor_edges()[0]->target());
    }

    TEST(JitGraphRewriter, DetachesInstructionWithoutPoisoningStorage)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ConstInstruction detached =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        ConstInstruction result =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &, const GraphQueries &, const Block &,
                const Instruction &instruction) {
                return instruction.id() == detached.id()
                           ? RewriteResult::detach()
                           : RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_FALSE(summary.terminators_changed);
        EXPECT_FALSE(summary.ir_level_changed);
        EXPECT_FALSE(detached.is_poisoned());
        EXPECT_EQ(InstructionKind::Const, detached.kind());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(result, entry->instruction_at(0));
        EXPECT_EQ(1u, graph->mutation_generation());
    }

    TEST(JitGraphRewriter, CommitsTargetIrLevelWithRewrite)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ConstInstruction result =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        rewriter.set_target_ir_level(IRLevel::Machine);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [](RewriteContext &, const GraphQueries &, const Block &,
               const Instruction &) { return RewriteResult::keep(); });

        EXPECT_FALSE(summary.instructions_changed);
        EXPECT_FALSE(summary.terminators_changed);
        EXPECT_TRUE(summary.ir_level_changed);
        EXPECT_EQ(IRLevel::Machine, graph->ir_level());
        EXPECT_EQ(1u, graph->mutation_generation());
    }

    TEST(JitGraphRewriter, CompactsBlockParametersAndIncomingArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 2> arguments = {ProgramValueRef(first),
                                                    ProgramValueRef(second)};
        BlockEdge *old_edge = builder.make_block_edge(entry, exit, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    old_edge);
        ParameterInstruction exit_first =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ParameterInstruction exit_second =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(exit_first));
        ControlFlowGraph *graph = builder.finalize();

        struct Callback
        {
            Instruction removed;

            BlockParameterRewrite block_parameter(RewriteContext &,
                                                  const GraphQueries &,
                                                  const Block &, size_t,
                                                  const Instruction &parameter)
            {
                return parameter.id() == removed.id()
                           ? BlockParameterRewrite::erase()
                           : BlockParameterRewrite::keep();
            }
        } callback{exit_second};

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), callback);

        EXPECT_TRUE(summary.block_parameters_changed);
        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_TRUE(exit_second.is_poisoned());
        ASSERT_EQ(1u, exit->parameters().size());
        EXPECT_EQ(exit_first, exit->parameter_at(0));
        BlockEdge *new_edge = entry->block_successor_edges()[0];
        EXPECT_NE(old_edge, new_edge);
        ASSERT_EQ(1u, new_edge->arguments().size());
        EXPECT_EQ(first.id(), new_edge->arguments()[0].instruction_id());
        ASSERT_EQ(1u, exit->predecessor_edges().size());
        EXPECT_EQ(new_edge, exit->predecessor_edges()[0]);
    }

    TEST(JitGraphRewriter,
         StructuralTransfersRedirectDefinitionsFromTheirInsertionPoint)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction old_return =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        struct Callback
        {
            Block *entry;
            ParameterInstruction parameter;
            ReturnInstruction return_instruction;
            std::optional<MovInstruction> entry_move;
            std::optional<MovInstruction> first_late_move;
            std::optional<MovInstruction> second_late_move;

            RewriteInsertion at_block_entry(RewriteContext &context,
                                            const GraphQueries &,
                                            const Block &block)
            {
                if(&block != entry)
                {
                    return RewriteInsertion::none();
                }
                entry_move = context.make_instruction<MovInstruction>(
                    TaggedValueRef(parameter));
                return RewriteInsertion::insert_transfers(
                    {*entry_move}, {{ProgramValueRef(parameter),
                                     ProgramValueRef(*entry_move)}});
            }

            RewriteInsertion before_instruction(RewriteContext &context,
                                                const GraphQueries &,
                                                const Block &,
                                                const Instruction &instruction)
            {
                if(instruction.id() != return_instruction.id())
                {
                    return RewriteInsertion::none();
                }
                first_late_move = context.make_instruction<MovInstruction>(
                    TaggedValueRef(parameter));
                second_late_move = context.make_instruction<MovInstruction>(
                    TaggedValueRef(*first_late_move));
                return RewriteInsertion::insert_transfers(
                    {*first_late_move, *second_late_move},
                    {{ProgramValueRef(parameter),
                      ProgramValueRef(*second_late_move)}});
            }
        } callback{entry, parameter, old_return, {}, {}, {}};

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(), RewriteInput::Normalized, callback);

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_TRUE(old_return.is_poisoned());
        ASSERT_EQ(4u, entry->instructions().size());
        ASSERT_TRUE(callback.entry_move.has_value());
        ASSERT_TRUE(callback.first_late_move.has_value());
        ASSERT_TRUE(callback.second_late_move.has_value());
        EXPECT_EQ(*callback.entry_move, entry->instruction_at(0));
        MovInstruction first_late_move =
            entry->instruction_at(1).as<MovInstruction>();
        MovInstruction second_late_move =
            entry->instruction_at(2).as<MovInstruction>();
        EXPECT_NE(*callback.first_late_move, first_late_move);
        EXPECT_NE(*callback.second_late_move, second_late_move);
        EXPECT_EQ(first_late_move.id(), summary.normalization_remapping.at(
                                            callback.first_late_move->id()));
        EXPECT_EQ(second_late_move.id(), summary.normalization_remapping.at(
                                             callback.second_late_move->id()));
        EXPECT_EQ(entry->instruction_at(3).id(),
                  summary.normalization_remapping.at(old_return.id()));
        EXPECT_FALSE(summary.normalization_remapping.contains(
            callback.entry_move->id()));
        EXPECT_EQ(parameter.id(),
                  callback.entry_move->source().instruction_id());
        EXPECT_EQ(callback.entry_move->id(),
                  first_late_move.source().instruction_id());
        EXPECT_EQ(first_late_move.id(),
                  second_late_move.source().instruction_id());
        EXPECT_EQ(second_late_move.id(), entry->instruction_at(3)
                                             .as<ReturnInstruction>()
                                             .return_value()
                                             .instruction_id());
    }

    TEST(JitGraphRewriter,
         StructuralTransfersApplySimultaneouslyBeforeEdgeArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 2> arguments = {ProgramValueRef(first),
                                                    ProgramValueRef(second)};
        BlockEdge *edge = builder.make_block_edge(entry, exit, arguments);
        UnconditionalBranchInstruction old_branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ParameterInstruction exit_first =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(exit_first));
        ControlFlowGraph *graph = builder.finalize();

        struct Callback
        {
            UnconditionalBranchInstruction branch;
            ParameterInstruction first;
            ParameterInstruction second;
            std::optional<MovInstruction> first_move;
            std::optional<MovInstruction> second_move;

            RewriteInsertion before_instruction(RewriteContext &context,
                                                const GraphQueries &,
                                                const Block &,
                                                const Instruction &instruction)
            {
                if(instruction.id() != branch.id())
                {
                    return RewriteInsertion::none();
                }
                first_move = context.make_instruction<MovInstruction>(
                    TaggedValueRef(second));
                second_move = context.make_instruction<MovInstruction>(
                    TaggedValueRef(first));
                return RewriteInsertion::insert_transfers(
                    {*first_move, *second_move},
                    {{ProgramValueRef(first), ProgramValueRef(*first_move)},
                     {ProgramValueRef(second), ProgramValueRef(*second_move)}});
            }
        } callback{old_branch, first, second, {}, {}};

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), callback);

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_TRUE(old_branch.is_poisoned());
        ASSERT_EQ(3u, entry->instructions().size());
        ASSERT_TRUE(callback.first_move.has_value());
        ASSERT_TRUE(callback.second_move.has_value());
        EXPECT_EQ(*callback.first_move, entry->instruction_at(0));
        EXPECT_EQ(*callback.second_move, entry->instruction_at(1));
        EXPECT_EQ(second.id(), callback.first_move->source().instruction_id());
        EXPECT_EQ(first.id(), callback.second_move->source().instruction_id());

        BlockEdge *new_edge = entry->terminator().block_successor_edges()[0];
        EXPECT_NE(edge, new_edge);
        ASSERT_EQ(2u, new_edge->arguments().size());
        EXPECT_EQ(callback.first_move->id(),
                  new_edge->arguments()[0].instruction_id());
        EXPECT_EQ(callback.second_move->id(),
                  new_edge->arguments()[1].instruction_id());
        EXPECT_EQ(entry->instruction_at(2).id(),
                  summary.normalization_remapping.at(old_branch.id()));
        EXPECT_FALSE(summary.normalization_remapping.contains(
            callback.first_move->id()));
        EXPECT_FALSE(summary.normalization_remapping.contains(
            callback.second_move->id()));
    }

    TEST(JitGraphRewriter, RejectsStructuralTransferOfAnUnavailableDefinition)
    {
        EXPECT_DEATH(
            {
                CompilationSession session;
                GraphBuilder builder(session, IRLevel::Core);
                Block *entry = builder.emplace_block();
                ConstInstruction constant =
                    builder.emplace_instruction<ConstInstruction>(
                        entry, Value::None());
                builder.emplace_instruction<ReturnInstruction>(
                    entry, TaggedValueRef(constant));
                ControlFlowGraph *graph = builder.finalize();

                struct Callback
                {
                    ConstInstruction constant;

                    RewriteInsertion at_block_entry(RewriteContext &context,
                                                    const GraphQueries &,
                                                    const Block &)
                    {
                        ConstInstruction replacement =
                            context.make_instruction<ConstInstruction>(
                                Value::True());
                        return RewriteInsertion::insert_transfers(
                            {replacement}, {{ProgramValueRef(constant),
                                             ProgramValueRef(replacement)}});
                    }
                } callback{constant};

                GraphRewriter rewriter(session, *graph);
                rewriter.rewrite_instructions(InstructionTraversal(), callback);
            },
            "not available at the insertion point");
    }

    TEST(JitGraphRewriter,
         RejectsStructuralTransferOutputNotEmittedByTheInsertion)
    {
        EXPECT_DEATH(
            {
                CompilationSession session;
                GraphBuilder builder(session, IRLevel::Core);
                Block *entry = builder.emplace_block();
                ParameterInstruction parameter =
                    builder.emplace_parameter<ParameterInstruction>(entry);
                builder.emplace_instruction<ReturnInstruction>(
                    entry, TaggedValueRef(parameter));
                ControlFlowGraph *graph = builder.finalize();

                struct Callback
                {
                    ParameterInstruction parameter;

                    RewriteInsertion at_block_entry(RewriteContext &context,
                                                    const GraphQueries &,
                                                    const Block &)
                    {
                        MovInstruction replacement =
                            context.make_instruction<MovInstruction>(
                                TaggedValueRef(parameter));
                        return RewriteInsertion::insert_transfers(
                            {}, {{ProgramValueRef(parameter),
                                  ProgramValueRef(replacement)}});
                    }
                } callback{parameter};

                GraphRewriter rewriter(session, *graph);
                rewriter.rewrite_instructions(InstructionTraversal(), callback);
            },
            "must be emitted by that insertion");
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
            GraphBuilder builder(session, IRLevel::Core);
            Block *entry = builder.emplace_block();
            ConstInstruction constant =
                builder.emplace_instruction<ConstInstruction>(entry,
                                                              Value::None());
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(constant));
            ControlFlowGraph *graph = builder.finalize();

            GraphRewriter rewriter(session, *graph);
            std::optional<ConstInstruction> folded_constant;
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
                        return RewriteResult::replace(*folded_constant);
                    }
                    return RewriteResult::keep();
                });

            EXPECT_TRUE(summary.instructions_changed);
            ASSERT_TRUE(folded_constant.has_value());
            EXPECT_EQ(value.raw_value(), folded_constant->constant());
            EXPECT_EQ(*folded_constant, entry->instruction_at(0));
            EXPECT_EQ(1, string->refcount);
        }
        EXPECT_EQ(0, string->refcount);
    }

    TEST(JitGraphRewriter,
         InsertsPrefixesAndSuffixesAroundTheCurrentInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction first = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        MovInstruction second = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(first));
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(second));
        ControlFlowGraph *graph = builder.finalize();

        std::optional<MovInstruction> prefix;
        std::optional<MovInstruction> suffix;
        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &context, const GraphQueries &, const Block &,
                const Instruction &instruction) {
                if(instruction.id() == first.id())
                {
                    prefix = context.make_instruction<MovInstruction>(
                        TaggedValueRef(parameter));
                    return RewriteResult::keep_with_prefix({*prefix});
                }
                if(instruction.id() == second.id())
                {
                    suffix = context.make_instruction<MovInstruction>(
                        TaggedValueRef(second));
                    return RewriteResult::keep_with_suffix({*suffix});
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_FALSE(summary.terminators_changed);
        EXPECT_EQ(1u, graph->mutation_generation());
        EXPECT_FALSE(first.is_poisoned());
        EXPECT_FALSE(second.is_poisoned());
        EXPECT_FALSE(return_instruction.is_poisoned());
        ASSERT_EQ(5u, entry->instructions().size());
        ASSERT_TRUE(prefix.has_value());
        ASSERT_TRUE(suffix.has_value());
        EXPECT_EQ(*prefix, entry->instruction_at(0));
        EXPECT_EQ(first, entry->instruction_at(1));
        EXPECT_EQ(second, entry->instruction_at(2));
        EXPECT_EQ(*suffix, entry->instruction_at(3));
        EXPECT_EQ(return_instruction, entry->instruction_at(4));
        EXPECT_EQ(second.id(), suffix->source().instruction_id());
    }

    TEST(JitGraphRewriter, ReplacesAnIdentityWithItsExistingDefinition)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        ReturnInstruction old_return =
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
                        instruction.as<MovInstruction>().source());
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_FALSE(summary.normalization_remapping.contains(move.id()));
        EXPECT_EQ(1u, graph->mutation_generation());
        EXPECT_TRUE(move.is_poisoned());
        EXPECT_TRUE(old_return.is_poisoned());
        ASSERT_EQ(1u, entry->instructions().size());
        ReturnInstruction new_return =
            entry->instruction_at(0).as<ReturnInstruction>();
        EXPECT_EQ(new_return.id(),
                  summary.normalization_remapping.at(old_return.id()));
        EXPECT_EQ(parameter.id(), new_return.return_value().instruction_id());

        GraphQueries rebuilt_queries = graph->prepare_queries(GraphQuery::Uses);
        EXPECT_EQ(1u, rebuilt_queries.uses_of(parameter).n_uses());
    }

    TEST(JitGraphRewriter, CanPassNormalizedInstructionsToTheCallback)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ConstInstruction old_constant =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        MovInstruction old_move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(old_constant));
        ReturnInstruction old_return =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(old_move));
        ControlFlowGraph *graph = builder.finalize();

        std::optional<ConstInstruction> new_constant;
        std::optional<MovInstruction> return_prefix;
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
                        return RewriteResult::replace(*new_constant);
                    case InstructionKind::Mov:
                        EXPECT_NE(old_move.id(), instruction.id());
                        EXPECT_EQ(new_constant->id(),
                                  instruction.as<MovInstruction>()
                                      .source()
                                      .instruction_id());
                        return RewriteResult::replace_with_def(
                            instruction.as<MovInstruction>().source());
                    case InstructionKind::Return:
                        EXPECT_NE(old_return.id(), instruction.id());
                        EXPECT_EQ(new_constant->id(),
                                  instruction.as<ReturnInstruction>()
                                      .return_value()
                                      .instruction_id());
                        return_prefix =
                            context.make_instruction<MovInstruction>(
                                TaggedValueRef(*new_constant));
                        return RewriteResult::keep_with_prefix(
                            {*return_prefix});
                    default:
                        break;
                }
                ADD_FAILURE() << "unexpected instruction kind";
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_TRUE(old_constant.is_poisoned());
        EXPECT_TRUE(old_move.is_poisoned());
        EXPECT_TRUE(old_return.is_poisoned());
        ASSERT_EQ(3u, entry->instructions().size());
        ASSERT_TRUE(new_constant.has_value());
        ASSERT_TRUE(return_prefix.has_value());
        EXPECT_EQ(*new_constant, entry->instruction_at(0));
        EXPECT_EQ(*return_prefix, entry->instruction_at(1));
        EXPECT_EQ(new_constant->id(), entry->instruction_at(2)
                                          .as<ReturnInstruction>()
                                          .return_value()
                                          .instruction_id());
    }

    TEST(JitGraphRewriter, RejectsUseListsWithNormalizedInput)
    {
        EXPECT_DEATH(
            {
                CompilationSession session;
                GraphBuilder builder(session, IRLevel::Core);
                Block *entry = builder.emplace_block();
                ConstInstruction constant =
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

    TEST(JitGraphRewriter, ReconstructsTerminatorForNormalizedEdgeArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ConstInstruction original =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(original)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        UnconditionalBranchInstruction old_terminator =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &context, const GraphQueries &, const Block &,
                const Instruction &instruction) {
                if(instruction.id() == original.id())
                {
                    return RewriteResult::replace(
                        context.make_instruction<ConstInstruction>(
                            Value::True()));
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_TRUE(original.is_poisoned());
        EXPECT_TRUE(old_terminator.is_poisoned());
        ASSERT_EQ(1u, edge->arguments().size());
        EXPECT_EQ(original.id(), edge->arguments()[0].instruction_id());

        BlockEdge *new_edge = entry->terminator().block_successor_edges()[0];
        EXPECT_NE(edge, new_edge);
        EXPECT_EQ(entry, new_edge->source());
        EXPECT_EQ(exit, new_edge->target());
        ASSERT_EQ(1u, new_edge->arguments().size());
        EXPECT_EQ(entry->instruction_at(0).id(),
                  new_edge->arguments()[0].instruction_id());
        ASSERT_EQ(1u, exit->predecessor_edges().size());
        EXPECT_EQ(new_edge, exit->predecessor_edges()[0]);
    }

    TEST(JitGraphRewriter, ReconstructsVariadicInstructionsFromTheSchema)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ConstInstruction callable =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(callable)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>(captured),
                BytecodePC{31});
        std::array<TaggedValueRef, 2> arguments = {TaggedValueRef(parameter),
                                                   TaggedValueRef(callable)};
        PythonCallInstruction call =
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
                if(instruction.id() == callable.id())
                {
                    return RewriteResult::replace(
                        context.make_instruction<ConstInstruction>(
                            Value::True()));
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(callable.is_poisoned());
        EXPECT_TRUE(snapshot.is_poisoned());
        EXPECT_TRUE(call.is_poisoned());
        ASSERT_EQ(4u, entry->instructions().size());
        auto new_callable = entry->instruction_at(0).as<ConstInstruction>();
        auto new_snapshot = entry->instruction_at(1).as<SnapshotInstruction>();
        auto new_call = entry->instruction_at(2).as<PythonCallInstruction>();
        EXPECT_EQ(Value::True(), new_callable.constant());
        EXPECT_EQ(new_callable.id(),
                  new_snapshot.captured_values()[0].instruction_id());
        EXPECT_EQ(new_callable.id(), new_call.callable().instruction_id());
        ASSERT_EQ(2u, new_call.arguments().size());
        EXPECT_EQ(parameter.id(), new_call.arguments()[0].instruction_id());
        EXPECT_EQ(new_callable.id(), new_call.arguments()[1].instruction_id());
        EXPECT_EQ(new_snapshot.id(), new_call.snapshot().instruction_id());
        EXPECT_EQ(BytecodePC{47}, new_call.interpreter_return_pc());
    }

    TEST(JitGraphRewriter, RewritesSideExitArgumentsWithoutChangingInputs)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> inputs = {ProgramValueRef(parameter)};
        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        std::array<ProgramValueRef, 1> region_values = {
            ProgramValueRef(region_parameter)};
        SnapshotInstruction region_snapshot =
            builder.make_instruction<SnapshotInstruction>(region_values,
                                                          BytecodePC{31});
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {region_snapshot.id()};
        SideExitRegionId region =
            builder.make_side_exit_region(parameter_ids, instructions)->id();
        ResumeInInterpreterWithSideExitRegionInstruction old_owner =
            builder.emplace_instruction<
                ResumeInInterpreterWithSideExitRegionInstruction>(entry, inputs,
                                                                  region);
        ControlFlowGraph *graph = builder.finalize();

        struct Callback
        {
            ParameterInstruction parameter;
            ResumeInInterpreterWithSideExitRegionInstruction owner;
            std::optional<MovInstruction> move;

            RewriteInsertion before_instruction(RewriteContext &context,
                                                const GraphQueries &,
                                                const Block &,
                                                const Instruction &instruction)
            {
                if(instruction.id() != owner.id())
                {
                    return RewriteInsertion::none();
                }
                move = context.make_instruction<MovInstruction>(
                    TaggedValueRef(parameter));
                return RewriteInsertion::insert_transfers(
                    {*move},
                    {{ProgramValueRef(parameter), ProgramValueRef(*move)}});
            }
        } callback{parameter, old_owner, {}};

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), callback);

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_TRUE(summary.terminators_changed);
        EXPECT_FALSE(parameter.is_poisoned());
        EXPECT_TRUE(old_owner.is_poisoned());
        ASSERT_TRUE(callback.move.has_value());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(*callback.move, entry->instruction_at(0));
        auto new_owner =
            entry->instruction_at(1)
                .as<ResumeInInterpreterWithSideExitRegionInstruction>();
        EXPECT_EQ(region, new_owner.side_exit_region());
        ASSERT_EQ(1u, new_owner.side_exit_arguments().size());
        EXPECT_EQ(callback.move->id(),
                  new_owner.side_exit_arguments()[0].instruction_id());
    }

    TEST(JitGraphRewriter, StagesSequencesAcrossTheWholeGraphBeforeCommit)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        UnconditionalBranchInstruction branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ConstInstruction constant =
            builder.emplace_instruction<ConstInstruction>(exit, Value::None());
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(constant));
        ControlFlowGraph *graph = builder.finalize();

        GraphRewriter rewriter(session, *graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &context, const GraphQueries &,
                const Block &block, const Instruction &instruction) {
                if(instruction.id() == move.id())
                {
                    MovInstruction first =
                        context.make_instruction<MovInstruction>(
                            TaggedValueRef(parameter));
                    MovInstruction second =
                        context.make_instruction<MovInstruction>(
                            TaggedValueRef(first));
                    return RewriteResult::replace({first, second},
                                                  TaggedValueRef(second));
                }
                if(block.serial() == exit->serial() &&
                   instruction.kind() == InstructionKind::Const)
                {
                    EXPECT_EQ(2u, entry->instructions().size());
                    EXPECT_EQ(move, entry->instruction_at(0));
                    EXPECT_EQ(branch, entry->instruction_at(1));
                    EXPECT_FALSE(move.is_poisoned());
                }
                return RewriteResult::keep();
            });

        EXPECT_TRUE(summary.instructions_changed);
        EXPECT_FALSE(summary.terminators_changed);
        EXPECT_TRUE(move.is_poisoned());
        ASSERT_EQ(3u, entry->instructions().size());
        auto first = entry->instruction_at(0).as<MovInstruction>();
        auto second = entry->instruction_at(1).as<MovInstruction>();
        EXPECT_EQ(parameter.id(), first.source().instruction_id());
        EXPECT_EQ(first.id(), second.source().instruction_id());
        EXPECT_EQ(branch, entry->instruction_at(2));
    }

    TEST(JitGraphRewriter, RejectsAUseOfAnErasedDefinition)
    {
        EXPECT_DEATH(
            {
                CompilationSession session;
                GraphBuilder builder(session, IRLevel::Core);
                Block *entry = builder.emplace_block();
                ConstInstruction constant =
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
                        if(instruction.id() == constant.id())
                        {
                            return RewriteResult::erase();
                        }
                        return RewriteResult::keep();
                    });
            },
            "erased definition");
    }

}  // namespace cl::jit
