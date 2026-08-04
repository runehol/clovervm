#include "jit/cfg_verifier.h"
#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_builder.h"
#include "jit/graph_queries.h"
#include "jit/instruction.h"
#include "jit/use_lists.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>

namespace cl::jit
{
    namespace
    {
        Value test_trusted_unary_handler(ThreadState *, Value value)
        {
            return value;
        }

        TrustedHandlerTarget test_trusted_handler_target()
        {
            return erase_trusted_handler_target(test_trusted_unary_handler);
        }

        TaggedValueRef emplace_constant(GraphBuilder &builder, Block *block,
                                        Value value)
        {
            return TaggedValueRef(
                builder.emplace_instruction<ConstInstruction>(block, value));
        }

        void expect_invalid_with(GraphBuilder &builder, const std::string &text)
        {
            EXPECT_DEATH(builder.finalize(), text);
        }

        void expect_invalid_trusted_handler_arity(size_t arity)
        {
            CompilationSession session;
            GraphBuilder builder(session, IRLevel::Core);
            Block *entry = builder.emplace_block();
            TaggedValueRef value =
                emplace_constant(builder, entry, Value::True());
            std::vector<TaggedValueRef> arguments(arity, value);
            TrustedHandlerCallInstruction call =
                builder.emplace_instruction<TrustedHandlerCallInstruction>(
                    entry, arguments, test_trusted_handler_target());
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(call));

            expect_invalid_with(builder, "unsupported trusted-handler arity");
        }
    }  // namespace

    TEST(JitCfg, ConditionalBranchExposesSemanticAndGenericEdges)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        builder.emplace_n_blocks(2);
        ASSERT_EQ(2u, builder.block_count());
        Block *entry = builder.block_at(0);
        Block *join = builder.block_at(1);
        BlockEdge *true_edge = builder.make_block_edge(entry, join);
        BlockEdge *false_edge = builder.make_block_edge(entry, join);
        Instruction branch_instruction =
            builder.make_instruction<ConditionalBranchInstruction>(
                emplace_constant(builder, entry, Value::True()), true_edge,
                false_edge);
        builder.append_instruction(entry, branch_instruction);
        builder.emplace_instruction<BareReturnInstruction>(
            join, emplace_constant(builder, join, Value::None()));

        TerminatorInstruction::BlockSuccessorEdges successors =
            entry->block_successor_edges();
        ASSERT_EQ(2u, successors.size());
        EXPECT_EQ(true_edge, successors[0]);
        EXPECT_EQ(false_edge, successors[1]);

        ControlFlowGraph *graph = builder.finalize();
        ASSERT_EQ(2u, join->predecessor_edges().size());
        EXPECT_EQ(true_edge, join->predecessor_edges()[0]);
        EXPECT_EQ(false_edge, join->predecessor_edges()[1]);
        EXPECT_EQ(entry, graph->entry_block());
        ASSERT_EQ(2u, graph->blocks().size());
        EXPECT_EQ(entry, graph->blocks()[0]);
        EXPECT_EQ(join, graph->blocks()[1]);
        EXPECT_TRUE(graph->is_published());
        EXPECT_EQ(IRLevel::Core, graph->ir_level());
    }

    TEST(JitCfg, RejectsInstructionsOutsideDeclaredIRLevel)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        SnapshotRef snapshot(builder.emplace_instruction<SnapshotInstruction>(
            entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{0}));
        builder.emplace_instruction<ResumeInInterpreterInstruction>(entry,
                                                                    snapshot);

        expect_invalid_with(builder, "is not legal in Machine IR");
    }

    TEST(JitCfg, ExplicitUnconditionalBranchAndReturnFormValidGraph)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        EXPECT_EQ(0u, entry->loop_depth());
        EXPECT_EQ(0u, exit->loop_depth());
        builder.set_loop_depth(exit, 2);
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        Instruction branch_instruction =
            builder.make_instruction<UnconditionalBranchInstruction>(edge);
        Instruction return_instruction =
            builder.make_instruction<BareReturnInstruction>(
                emplace_constant(builder, exit, Value::None()));
        builder.append_instruction(entry, branch_instruction);
        builder.append_instruction(exit, return_instruction);

        TerminatorInstruction::BlockSuccessorEdges successors =
            entry->block_successor_edges();
        ASSERT_EQ(1u, successors.size());
        EXPECT_EQ(edge, successors[0]);
        EXPECT_TRUE(exit->block_successor_edges().empty());
        EXPECT_EQ(InstructionKind::BareReturn, return_instruction.kind());
        ControlFlowGraph *graph = builder.finalize();
        EXPECT_TRUE(graph->is_published());
        EXPECT_EQ(0u, graph->blocks()[0]->loop_depth());
        EXPECT_EQ(2u, graph->blocks()[1]->loop_depth());
    }

    TEST(JitCfg, ResumeInInterpreterTerminatesItsBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        SnapshotRef snapshot(builder.emplace_instruction<SnapshotInstruction>(
            entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7}));
        ResumeInInterpreterInstruction resume =
            builder.emplace_instruction<ResumeInInterpreterInstruction>(
                entry, snapshot);
        EXPECT_TRUE(resume.is_block_terminator());
        ControlFlowGraph *graph = builder.finalize();
        EXPECT_TRUE(graph->is_published());
        EXPECT_EQ(2u, entry->instructions().size());
    }

    TEST(JitCfgVerifier, RejectsUnsupportedTrustedHandlerCallArities)
    {
        expect_invalid_trusted_handler_arity(0);
        expect_invalid_trusted_handler_arity(4);
    }

    TEST(JitCfg, OwnsSideExitRegionAndBindingArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        TaggedValueRef source = emplace_constant(builder, entry, Value::True());
        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        std::array<ProgramValueRef, 1> snapshot_values = {
            ProgramValueRef(region_parameter)};
        ExitToInterpreterInstruction exit =
            builder.make_instruction<ExitToInterpreterInstruction>(
                snapshot_values, BytecodePCOffset{7});
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {exit.id()};
        SideExitRegion *region =
            builder.make_side_exit_region(parameter_ids, instructions);
        std::array<ProgramValueRef, 1> arguments = {source};
        ResumeInInterpreterWithSideExitInstruction owner =
            builder.emplace_instruction<
                ResumeInInterpreterWithSideExitInstruction>(entry, arguments,
                                                            region->id());

        ControlFlowGraph *graph = builder.finalize();

        const SideExitRegion &stored =
            graph->storage()->side_exit_region(region->id());
        ASSERT_EQ(1u, stored.parameter_ids().size());
        EXPECT_EQ(region_parameter.id(), stored.parameter_ids()[0]);
        ASSERT_EQ(1u, stored.instruction_ids().size());
        EXPECT_EQ(exit.id(), stored.instruction_ids()[0]);
        EXPECT_EQ(region->id(), owner.side_exit_region());
        ASSERT_EQ(1u, owner.side_exit_arguments().size());
        EXPECT_EQ(source.instruction_id(),
                  owner.side_exit_arguments()[0].instruction_id());

        GraphQueries queries = graph->prepare_queries(GraphQuery::Uses);
        const Uses &uses = queries.uses_of(
            graph->storage()->instruction(source.instruction_id()));
        ASSERT_EQ(1u, uses.n_instruction_uses());
        EXPECT_EQ(owner.id(), uses.instruction_uses()[0].instruction);
        EXPECT_EQ(ResumeInInterpreterWithSideExitInstruction::
                      side_exit_arguments_operand_index,
                  uses.instruction_uses()[0].operand_index);
    }

    TEST(JitCfgVerifier, RejectsSideExitBindingWithWrongArity)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        std::array<ProgramValueRef, 1> snapshot_values = {
            ProgramValueRef(region_parameter)};
        ExitToInterpreterInstruction exit =
            builder.make_instruction<ExitToInterpreterInstruction>(
                snapshot_values, BytecodePCOffset{7});
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {exit.id()};
        SideExitRegion *region =
            builder.make_side_exit_region(parameter_ids, instructions);
        builder.emplace_instruction<ResumeInInterpreterWithSideExitInstruction>(
            entry, std::span<const ProgramValueRef>{}, region->id());

        expect_invalid_with(builder, "wrong number of side-exit arguments");
    }

    TEST(JitCfgVerifier, RejectsSideExitBindingWithWrongRepresentation)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        TaggedValueRef source = emplace_constant(builder, entry, Value::True());
        ParameterF64Instruction region_parameter =
            builder.make_instruction<ParameterF64Instruction>();
        std::array<ProgramValueRef, 1> snapshot_values = {
            ProgramValueRef(region_parameter)};
        ExitToInterpreterInstruction exit =
            builder.make_instruction<ExitToInterpreterInstruction>(
                snapshot_values, BytecodePCOffset{7});
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {exit.id()};
        SideExitRegion *region =
            builder.make_side_exit_region(parameter_ids, instructions);
        std::array<ProgramValueRef, 1> arguments = {source};
        builder.emplace_instruction<ResumeInInterpreterWithSideExitInstruction>(
            entry, arguments, region->id());

        expect_invalid_with(builder, "incompatible representation");
    }

    TEST(JitCfgVerifier, RejectsSideExitRegionWithoutFinalExitToInterpreter)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        TaggedValueRef source = emplace_constant(builder, entry, Value::True());
        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        MovInstruction move = builder.make_instruction<MovInstruction>(
            TaggedValueRef(region_parameter));
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {move.id()};
        SideExitRegion *region =
            builder.make_side_exit_region(parameter_ids, instructions);
        std::array<ProgramValueRef, 1> arguments = {source};
        builder.emplace_instruction<ResumeInInterpreterWithSideExitInstruction>(
            entry, arguments, region->id());

        expect_invalid_with(builder, "does not end in ExitToInterpreter");
    }

    TEST(JitCfgVerifier, RejectsSideExitRegionReferenceOutsideRegion)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        TaggedValueRef source = emplace_constant(builder, entry, Value::True());
        std::array<ProgramValueRef, 1> snapshot_values = {source};
        ExitToInterpreterInstruction exit =
            builder.make_instruction<ExitToInterpreterInstruction>(
                snapshot_values, BytecodePCOffset{7});
        std::array<InstructionId, 0> parameter_ids = {};
        std::array<InstructionId, 1> instructions = {exit.id()};
        SideExitRegion *region =
            builder.make_side_exit_region(parameter_ids, instructions);
        builder.emplace_instruction<ResumeInInterpreterWithSideExitInstruction>(
            entry, std::span<const ProgramValueRef>{}, region->id());

        expect_invalid_with(builder, "outside the region or before");
    }

    TEST(JitCfg, ParallelEdgesCarryIndependentOrderedArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *join = builder.emplace_block();
        TaggedValueRef true_value =
            emplace_constant(builder, entry, Value::True());
        TaggedValueRef false_value =
            emplace_constant(builder, entry, Value::False());
        std::array<ProgramValueRef, 1> true_arguments = {true_value};
        std::array<ProgramValueRef, 1> false_arguments = {false_value};
        BlockEdge *true_edge = builder.make_block_edge(
            entry, join, std::span<const ProgramValueRef>(true_arguments));
        BlockEdge *false_edge = builder.make_block_edge(
            entry, join, std::span<const ProgramValueRef>(false_arguments));
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, true_value, true_edge, false_edge);
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(join);
        builder.emplace_instruction<BareReturnInstruction>(
            join, TaggedValueRef(parameter));

        ControlFlowGraph *graph = builder.finalize();

        EXPECT_TRUE(graph->is_published());
        ASSERT_EQ(1u, true_edge->arguments().size());
        EXPECT_EQ(true_value.instruction_id(),
                  true_edge->arguments()[0].instruction_id());
        ASSERT_EQ(1u, false_edge->arguments().size());
        EXPECT_EQ(false_value.instruction_id(),
                  false_edge->arguments()[0].instruction_id());
    }

    TEST(JitCfg, EntryParametersArePlacedSeparatelyFromInstructions)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction tagged_parameter =
            builder.make_instruction<ParameterInstruction>();
        builder.append_parameter(entry, tagged_parameter);
        ParameterF64Instruction f64_parameter =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ParameterPointerInstruction pointer_parameter =
            builder.emplace_parameter<ParameterPointerInstruction>(entry);
        BareReturnInstruction return_instruction =
            builder.make_instruction<BareReturnInstruction>(
                TaggedValueRef(tagged_parameter));

        builder.append_instruction(entry, return_instruction);
        ControlFlowGraph *graph = builder.finalize();
        EXPECT_TRUE(graph->is_published());

        ASSERT_EQ(3u, entry->parameters().size());
        EXPECT_EQ(tagged_parameter, entry->parameter_at(0));
        EXPECT_EQ(f64_parameter, entry->parameter_at(1));
        EXPECT_EQ(pointer_parameter, entry->parameter_at(2));
        ASSERT_EQ(1u, entry->instructions().size());
        EXPECT_EQ(return_instruction, entry->instruction_at(0));
    }

    TEST(JitCfg, ParametersMayBelongToAnyBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        TaggedValueRef argument =
            emplace_constant(builder, entry, Value::None());
        std::array<ProgramValueRef, 1> arguments = {argument};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<BareReturnInstruction>(
            exit, emplace_constant(builder, exit, Value::None()));

        ControlFlowGraph *graph = builder.finalize();

        EXPECT_TRUE(graph->is_published());
        ASSERT_EQ(1u, exit->parameters().size());
        EXPECT_EQ(parameter, exit->parameter_at(0));
    }

    TEST(JitCfgVerifier, RejectsWrongBlockArgumentArity)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<BareReturnInstruction>(
            exit, emplace_constant(builder, exit, Value::None()));

        expect_invalid_with(builder, "supplies 0 arguments for 1 target");
    }

    TEST(JitCfgVerifier, RejectsIncompatibleBlockArgumentRepresentation)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterF64Instruction argument =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(argument)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<BareReturnInstruction>(
            exit, emplace_constant(builder, exit, Value::None()));

        expect_invalid_with(builder, "incompatible value representation");
    }

    TEST(JitCfgVerifier, RejectsUnavailableBlockArgument)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction unavailable =
            builder.emplace_parameter<ParameterInstruction>(exit);
        std::array<ProgramValueRef, 1> arguments = {
            ProgramValueRef(unavailable)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        builder.emplace_instruction<BareReturnInstruction>(
            exit, TaggedValueRef(unavailable));

        expect_invalid_with(builder, "outside its source block or after");
    }

    TEST(JitCfgVerifier, RejectsEmptyBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        builder.emplace_block();

        expect_invalid_with(builder, "has no instructions");
    }

    TEST(JitCfgVerifier, RejectsNonTerminatorAsFinalInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        builder.append_instruction(
            entry, builder.make_instruction<ConstInstruction>(Value::None()));

        expect_invalid_with(builder, "does not end in a block terminator");
    }

    TEST(JitCfgVerifier, RejectsTerminatorBeforeFinalInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef none = emplace_constant(builder, entry, Value::None());
        builder.append_instruction(
            entry, builder.make_instruction<BareReturnInstruction>(none));
        builder.append_instruction(
            entry, builder.make_instruction<BareReturnInstruction>(none));

        expect_invalid_with(builder,
                            "block terminator before its final instruction");
    }

    TEST(JitCfgVerifier, RejectsConditionalBranchThatReusesOneEdge)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.append_instruction(
            entry,
            builder.make_instruction<ConditionalBranchInstruction>(
                emplace_constant(builder, entry, Value::True()), edge, edge));
        builder.append_instruction(
            exit, builder.make_instruction<BareReturnInstruction>(
                      emplace_constant(builder, exit, Value::None())));

        expect_invalid_with(builder, "reuses one block edge");
    }

    TEST(JitCfgVerifier, RejectsEdgeReferencedByTheWrongSource)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *declared_source = builder.emplace_block();
        Block *actual_source = builder.emplace_block();
        Block *target = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(declared_source, target);
        builder.append_instruction(
            declared_source,
            builder.make_instruction<BareReturnInstruction>(
                emplace_constant(builder, declared_source, Value::None())));
        builder.append_instruction(
            actual_source,
            builder.make_instruction<UnconditionalBranchInstruction>(edge));
        builder.append_instruction(
            target, builder.make_instruction<BareReturnInstruction>(
                        emplace_constant(builder, target, Value::None())));

        expect_invalid_with(builder, "as its source but is referenced by");
    }

    TEST(JitCfgVerifier, RejectsReferenceToAnUnplacedInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction unplaced =
            builder.make_instruction<ParameterInstruction>();
        builder.append_instruction(
            entry, builder.make_instruction<BareReturnInstruction>(
                       TaggedValueRef(unplaced)));

        expect_invalid_with(builder,
                            "outside its block or before its definition");
    }

    TEST(JitCfgVerifier, RejectsReferenceAcrossBlocks)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction parameter =
            builder.make_instruction<ParameterInstruction>();
        builder.append_parameter(entry, parameter);
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.append_instruction(
            entry,
            builder.make_instruction<UnconditionalBranchInstruction>(edge));
        builder.append_instruction(
            exit, builder.make_instruction<BareReturnInstruction>(
                      TaggedValueRef(parameter)));

        expect_invalid_with(builder,
                            "outside its block or before its definition");
    }

    TEST(JitCfg, OneStorageCanOwnMultipleGraphs)
    {
        CompilationSession session;
        GraphBuilder first_builder(session, IRLevel::Core);
        Block *first_entry = first_builder.emplace_block();
        first_builder.append_instruction(
            first_entry,
            first_builder.make_instruction<BareReturnInstruction>(
                emplace_constant(first_builder, first_entry, Value::None())));
        ControlFlowGraph *first_graph = first_builder.finalize();

        GraphBuilder second_builder(session, IRLevel::Core);
        Block *second_entry = second_builder.emplace_block();
        second_builder.append_instruction(
            second_entry,
            second_builder.make_instruction<BareReturnInstruction>(
                emplace_constant(second_builder, second_entry, Value::None())));
        ControlFlowGraph *second_graph = second_builder.finalize();

        EXPECT_NE(first_graph, second_graph);
        EXPECT_NE(first_graph->serial(), second_graph->serial());
        EXPECT_TRUE(first_graph->is_published());
        EXPECT_TRUE(second_graph->is_published());
    }

}  // namespace cl::jit
