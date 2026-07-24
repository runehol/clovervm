#include "jit/cfg_verifier.h"
#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_builder.h"
#include "jit/instruction.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>

namespace cl::jit
{
    namespace
    {
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
    }  // namespace

    TEST(JitCfg, ConditionalBranchExposesSemanticAndGenericEdges)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        builder.emplace_n_blocks(2);
        ASSERT_EQ(2u, builder.block_count());
        Block *entry = builder.block_at(0);
        Block *join = builder.block_at(1);
        BlockEdge *true_edge = builder.make_block_edge(entry, join);
        BlockEdge *false_edge = builder.make_block_edge(entry, join);
        Instruction *branch_instruction =
            builder.make_instruction<ConditionalBranchInstruction>(
                emplace_constant(builder, entry, Value::True()), true_edge,
                false_edge);
        builder.append_instruction(entry, branch_instruction);
        builder.emplace_instruction<ReturnInstruction>(
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
    }

    TEST(JitCfg, ExplicitUnconditionalBranchAndReturnFormValidGraph)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        Instruction *branch_instruction =
            builder.make_instruction<UnconditionalBranchInstruction>(edge);
        Instruction *return_instruction =
            builder.make_instruction<ReturnInstruction>(
                emplace_constant(builder, exit, Value::None()));
        builder.append_instruction(entry, branch_instruction);
        builder.append_instruction(exit, return_instruction);

        TerminatorInstruction::BlockSuccessorEdges successors =
            entry->block_successor_edges();
        ASSERT_EQ(1u, successors.size());
        EXPECT_EQ(edge, successors[0]);
        EXPECT_TRUE(exit->block_successor_edges().empty());
        EXPECT_EQ(InstructionKind::Return, return_instruction->kind());
        ControlFlowGraph *graph = builder.finalize();
        EXPECT_TRUE(graph->is_published());
    }

    TEST(JitCfg, ParallelEdgesCarryIndependentOrderedArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session);
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
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(join);
        builder.emplace_instruction<ReturnInstruction>(
            join, TaggedValueRef(parameter));

        ControlFlowGraph *graph = builder.finalize();

        EXPECT_TRUE(graph->is_published());
        ASSERT_EQ(1u, true_edge->arguments().size());
        EXPECT_EQ(true_value.instruction(),
                  true_edge->arguments()[0].instruction());
        ASSERT_EQ(1u, false_edge->arguments().size());
        EXPECT_EQ(false_value.instruction(),
                  false_edge->arguments()[0].instruction());
    }

    TEST(JitCfg, EntryParametersArePlacedSeparatelyFromInstructions)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *tagged_parameter =
            builder.make_instruction<ParameterInstruction>();
        builder.append_parameter(entry, tagged_parameter);
        ParameterF64Instruction *f64_parameter =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ReturnInstruction *return_instruction =
            builder.make_instruction<ReturnInstruction>(
                TaggedValueRef(tagged_parameter));

        builder.append_instruction(entry, return_instruction);
        ControlFlowGraph *graph = builder.finalize();
        EXPECT_TRUE(graph->is_published());

        ASSERT_EQ(2u, entry->parameters().size());
        EXPECT_EQ(tagged_parameter, entry->parameters()[0]);
        EXPECT_EQ(f64_parameter, entry->parameters()[1]);
        ASSERT_EQ(1u, entry->instructions().size());
        EXPECT_EQ(return_instruction, entry->instructions()[0]);
    }

    TEST(JitCfg, ParametersMayBelongToAnyBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        TaggedValueRef argument =
            emplace_constant(builder, entry, Value::None());
        std::array<ProgramValueRef, 1> arguments = {argument};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, emplace_constant(builder, exit, Value::None()));

        ControlFlowGraph *graph = builder.finalize();

        EXPECT_TRUE(graph->is_published());
        ASSERT_EQ(1u, exit->parameters().size());
        EXPECT_EQ(parameter, exit->parameters()[0]);
    }

    TEST(JitCfgVerifier, RejectsWrongBlockArgumentArity)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, emplace_constant(builder, exit, Value::None()));

        expect_invalid_with(builder, "supplies 0 arguments for 1 target");
    }

    TEST(JitCfgVerifier, RejectsIncompatibleBlockArgumentRepresentation)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterF64Instruction *argument =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(argument)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, emplace_constant(builder, exit, Value::None()));

        expect_invalid_with(builder, "incompatible value representation");
    }

    TEST(JitCfgVerifier, RejectsUnavailableBlockArgument)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction *unavailable =
            builder.emplace_parameter<ParameterInstruction>(exit);
        std::array<ProgramValueRef, 1> arguments = {
            ProgramValueRef(unavailable)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(unavailable));

        expect_invalid_with(builder, "outside its source block or after");
    }

    TEST(JitCfgVerifier, RejectsEmptyBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        builder.emplace_block();

        expect_invalid_with(builder, "has no instructions");
    }

    TEST(JitCfgVerifier, RejectsNonTerminatorAsFinalInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        builder.append_instruction(
            entry, builder.make_instruction<ConstInstruction>(Value::None()));

        expect_invalid_with(builder, "does not end in a block terminator");
    }

    TEST(JitCfgVerifier, RejectsTerminatorBeforeFinalInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef none = emplace_constant(builder, entry, Value::None());
        builder.append_instruction(
            entry, builder.make_instruction<ReturnInstruction>(none));
        builder.append_instruction(
            entry, builder.make_instruction<ReturnInstruction>(none));

        expect_invalid_with(builder,
                            "block terminator before its final instruction");
    }

    TEST(JitCfgVerifier, RejectsConditionalBranchThatReusesOneEdge)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.append_instruction(
            entry,
            builder.make_instruction<ConditionalBranchInstruction>(
                emplace_constant(builder, entry, Value::True()), edge, edge));
        builder.append_instruction(
            exit, builder.make_instruction<ReturnInstruction>(
                      emplace_constant(builder, exit, Value::None())));

        expect_invalid_with(builder, "reuses one block edge");
    }

    TEST(JitCfgVerifier, RejectsEdgeReferencedByTheWrongSource)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *declared_source = builder.emplace_block();
        Block *actual_source = builder.emplace_block();
        Block *target = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(declared_source, target);
        builder.append_instruction(
            declared_source,
            builder.make_instruction<ReturnInstruction>(
                emplace_constant(builder, declared_source, Value::None())));
        builder.append_instruction(
            actual_source,
            builder.make_instruction<UnconditionalBranchInstruction>(edge));
        builder.append_instruction(
            target, builder.make_instruction<ReturnInstruction>(
                        emplace_constant(builder, target, Value::None())));

        expect_invalid_with(builder, "as its source but is referenced by");
    }

    TEST(JitCfgVerifier, RejectsReferenceToAnUnplacedInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *unplaced =
            builder.make_instruction<ParameterInstruction>();
        builder.append_instruction(entry,
                                   builder.make_instruction<ReturnInstruction>(
                                       TaggedValueRef(unplaced)));

        expect_invalid_with(builder,
                            "outside its block or before its definition");
    }

    TEST(JitCfgVerifier, RejectsReferenceAcrossBlocks)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.make_instruction<ParameterInstruction>();
        builder.append_parameter(entry, parameter);
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.append_instruction(
            entry,
            builder.make_instruction<UnconditionalBranchInstruction>(edge));
        builder.append_instruction(exit,
                                   builder.make_instruction<ReturnInstruction>(
                                       TaggedValueRef(parameter)));

        expect_invalid_with(builder,
                            "outside its block or before its definition");
    }

    TEST(JitCfg, OneArenaCanOwnMultipleGraphs)
    {
        CompilationSession session;
        GraphBuilder first_builder(session);
        Block *first_entry = first_builder.emplace_block();
        first_builder.append_instruction(
            first_entry,
            first_builder.make_instruction<ReturnInstruction>(
                emplace_constant(first_builder, first_entry, Value::None())));
        ControlFlowGraph *first_graph = first_builder.finalize();

        GraphBuilder second_builder(session);
        Block *second_entry = second_builder.emplace_block();
        second_builder.append_instruction(
            second_entry,
            second_builder.make_instruction<ReturnInstruction>(
                emplace_constant(second_builder, second_entry, Value::None())));
        ControlFlowGraph *second_graph = second_builder.finalize();

        EXPECT_NE(first_graph, second_graph);
        EXPECT_NE(first_graph->serial(), second_graph->serial());
        EXPECT_TRUE(first_graph->is_published());
        EXPECT_TRUE(second_graph->is_published());
    }

}  // namespace cl::jit
