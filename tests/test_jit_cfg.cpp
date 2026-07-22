#include "jit/cfg_verifier.h"
#include "jit/compilation_arena.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_builder.h"
#include "jit/instruction.h"

#include <gtest/gtest.h>

#include <string>

namespace cl::jit
{
    namespace
    {
        TaggedValueOperand none_value()
        {
            return TaggedValueOperand(InlineValueConstant(Value::None()));
        }

        void expect_invalid_with(GraphBuilder &builder, const std::string &text)
        {
            EXPECT_DEATH(builder.finalize(), text);
        }
    }  // namespace

    TEST(JitCfg, ConditionalBranchExposesSemanticAndGenericEdges)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        builder.emplace_n_blocks(2);
        ASSERT_EQ(2u, builder.block_count());
        Block *entry = builder.block_at(0);
        Block *join = builder.block_at(1);
        BlockEdge *true_edge = builder.make_block_edge(entry, join);
        BlockEdge *false_edge = builder.make_block_edge(entry, join);
        Instruction *branch_instruction =
            builder.make_instruction<ConditionalBranchInstruction>(
                TaggedValueOperand(InlineValueConstant(Value::True())),
                true_edge, false_edge);
        builder.append_instruction(entry, branch_instruction);
        builder.emplace_instruction<ReturnInstruction>(join, none_value());

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
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        Instruction *branch_instruction =
            builder.make_instruction<UnconditionalBranchInstruction>(edge);
        Instruction *return_instruction =
            builder.make_instruction<ReturnInstruction>(none_value());
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

    TEST(JitCfg, EntryParametersArePlacedSeparatelyFromInstructions)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        ParameterInstruction *tagged_parameter =
            builder.make_instruction<ParameterInstruction>();
        builder.append_parameter(entry, tagged_parameter);
        ParameterF64Instruction *f64_parameter =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ReturnInstruction *return_instruction =
            builder.make_instruction<ReturnInstruction>(
                TaggedValueOperand(TaggedValueRef(tagged_parameter)));

        builder.append_instruction(entry, return_instruction);
        ControlFlowGraph *graph = builder.finalize();
        EXPECT_TRUE(graph->is_published());

        ASSERT_EQ(2u, entry->parameters().size());
        EXPECT_EQ(tagged_parameter, entry->parameters()[0]);
        EXPECT_EQ(f64_parameter, entry->parameters()[1]);
        ASSERT_EQ(1u, entry->instructions().size());
        EXPECT_EQ(return_instruction, entry->instructions()[0]);
    }

    TEST(JitCfgVerifier, RejectsEmptyBlock)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        builder.emplace_block();

        expect_invalid_with(builder, "has no instructions");
    }

    TEST(JitCfgVerifier, RejectsNonTerminatorAsFinalInstruction)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        builder.append_instruction(
            entry, builder.make_instruction<SynthesizeImmediateInstruction>(
                       InlineValueConstant(Value::None())));

        expect_invalid_with(builder, "does not end in a block terminator");
    }

    TEST(JitCfgVerifier, RejectsTerminatorBeforeFinalInstruction)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        builder.append_instruction(
            entry, builder.make_instruction<ReturnInstruction>(none_value()));
        builder.append_instruction(
            entry, builder.make_instruction<ReturnInstruction>(none_value()));

        expect_invalid_with(builder,
                            "block terminator before its final instruction");
    }

    TEST(JitCfgVerifier, RejectsConditionalBranchThatReusesOneEdge)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.append_instruction(
            entry, builder.make_instruction<ConditionalBranchInstruction>(
                       TaggedValueOperand(InlineValueConstant(Value::True())),
                       edge, edge));
        builder.append_instruction(
            exit, builder.make_instruction<ReturnInstruction>(none_value()));

        expect_invalid_with(builder, "reuses one block edge");
    }

    TEST(JitCfgVerifier, RejectsEdgeReferencedByTheWrongSource)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *declared_source = builder.emplace_block();
        Block *actual_source = builder.emplace_block();
        Block *target = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(declared_source, target);
        builder.append_instruction(
            declared_source,
            builder.make_instruction<ReturnInstruction>(none_value()));
        builder.append_instruction(
            actual_source,
            builder.make_instruction<UnconditionalBranchInstruction>(edge));
        builder.append_instruction(
            target, builder.make_instruction<ReturnInstruction>(none_value()));

        expect_invalid_with(builder, "as its source but is referenced by");
    }

    TEST(JitCfgVerifier, RejectsReferenceToAnUnplacedInstruction)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        ParameterInstruction *unplaced =
            builder.make_instruction<ParameterInstruction>();
        builder.append_instruction(
            entry, builder.make_instruction<ReturnInstruction>(
                       TaggedValueOperand(TaggedValueRef(unplaced))));

        expect_invalid_with(builder,
                            "outside its block or before its definition");
    }

    TEST(JitCfgVerifier, RejectsReferenceAcrossBlocks)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.make_instruction<ParameterInstruction>();
        builder.append_parameter(entry, parameter);
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        builder.append_instruction(
            entry,
            builder.make_instruction<UnconditionalBranchInstruction>(edge));
        builder.append_instruction(
            exit, builder.make_instruction<ReturnInstruction>(
                      TaggedValueOperand(TaggedValueRef(parameter))));

        expect_invalid_with(builder,
                            "outside its block or before its definition");
    }

    TEST(JitCfg, OneArenaCanOwnMultipleGraphs)
    {
        CompilationArena arena;
        GraphBuilder first_builder(arena);
        Block *first_entry = first_builder.emplace_block();
        first_builder.append_instruction(
            first_entry,
            first_builder.make_instruction<ReturnInstruction>(none_value()));
        ControlFlowGraph *first_graph = first_builder.finalize();

        GraphBuilder second_builder(arena);
        Block *second_entry = second_builder.emplace_block();
        second_builder.append_instruction(
            second_entry,
            second_builder.make_instruction<ReturnInstruction>(none_value()));
        ControlFlowGraph *second_graph = second_builder.finalize();

        EXPECT_NE(first_graph, second_graph);
        EXPECT_NE(first_graph->serial(), second_graph->serial());
        EXPECT_TRUE(first_graph->is_published());
        EXPECT_TRUE(second_graph->is_published());
    }

}  // namespace cl::jit
