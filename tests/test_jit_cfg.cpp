#include "jit/cfg_verifier.h"
#include "jit/compilation_arena.h"
#include "jit/control_flow_graph.h"
#include "jit/instruction.h"

#include <gtest/gtest.h>

#include <string>

namespace cl::jit
{
    namespace
    {
        ProgramValueOperand none_value()
        {
            return ProgramValueOperand(InlineValueConstant(Value::None()));
        }

        void expect_valid(const ControlFlowGraph &graph)
        {
            CfgVerificationResult result = verify_cfg(graph);
            EXPECT_TRUE(result.valid) << result.message;
        }

        void expect_invalid_with(const ControlFlowGraph &graph,
                                 const std::string &text)
        {
            CfgVerificationResult result = verify_cfg(graph);
            ASSERT_FALSE(result.valid);
            EXPECT_NE(std::string::npos, result.message.find(text))
                << result.message;
        }
    }  // namespace

    TEST(JitCfg, ConditionalBranchExposesSemanticAndGenericEdges)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        Block *join = graph.add_block();
        BlockEdge *true_edge = graph.make_block_edge(entry, join);
        BlockEdge *false_edge = graph.make_block_edge(entry, join);
        Instruction *branch_instruction = arena.make_conditional_branch(
            ProgramValueOperand(InlineValueConstant(Value::True())), true_edge,
            false_edge);
        entry->append_instruction(branch_instruction);
        join->append_instruction(arena.make_return(none_value()));

        EXPECT_EQ(entry, graph.entry_block());
        ASSERT_EQ(2u, graph.blocks().size());
        EXPECT_EQ(entry, graph.blocks()[0]);
        EXPECT_EQ(join, graph.blocks()[1]);

        TerminatorInstruction::BlockSuccessorEdges successors =
            entry->block_successor_edges();
        ASSERT_EQ(2u, successors.size());
        EXPECT_EQ(true_edge, successors[0]);
        EXPECT_EQ(false_edge, successors[1]);

        ASSERT_EQ(2u, join->predecessor_edges().size());
        EXPECT_EQ(true_edge, join->predecessor_edges()[0]);
        EXPECT_EQ(false_edge, join->predecessor_edges()[1]);
        expect_valid(graph);
    }

    TEST(JitCfg, ExplicitUnconditionalBranchAndReturnFormValidGraph)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        Block *exit = graph.add_block();
        BlockEdge *edge = graph.make_block_edge(entry, exit);
        Instruction *branch_instruction = arena.make_unconditional_branch(edge);
        Instruction *return_instruction = arena.make_return(none_value());
        entry->append_instruction(branch_instruction);
        exit->append_instruction(return_instruction);

        TerminatorInstruction::BlockSuccessorEdges successors =
            entry->block_successor_edges();
        ASSERT_EQ(1u, successors.size());
        EXPECT_EQ(edge, successors[0]);
        EXPECT_TRUE(exit->block_successor_edges().empty());
        EXPECT_EQ(InstructionKind::Return, return_instruction->kind());
        expect_valid(graph);
    }

    TEST(JitCfgVerifier, RejectsEmptyBlock)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        graph.add_block();

        expect_invalid_with(graph, "has no instructions");
    }

    TEST(JitCfgVerifier, RejectsNonTerminatorAsFinalInstruction)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        entry->append_instruction(arena.make_parameter().instruction());

        expect_invalid_with(graph, "does not end in a block terminator");
    }

    TEST(JitCfgVerifier, RejectsTerminatorBeforeFinalInstruction)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        entry->append_instruction(arena.make_return(none_value()));
        entry->append_instruction(arena.make_return(none_value()));

        expect_invalid_with(graph,
                            "block terminator before its final instruction");
    }

    TEST(JitCfgVerifier, RejectsConditionalBranchThatReusesOneEdge)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        Block *exit = graph.add_block();
        BlockEdge *edge = graph.make_block_edge(entry, exit);
        entry->append_instruction(arena.make_conditional_branch(
            ProgramValueOperand(InlineValueConstant(Value::True())), edge,
            edge));
        exit->append_instruction(arena.make_return(none_value()));

        expect_invalid_with(graph, "reuses one block edge");
    }

    TEST(JitCfgVerifier, RejectsEdgeReferencedByTheWrongSource)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *declared_source = graph.add_block();
        Block *actual_source = graph.add_block();
        Block *target = graph.add_block();
        BlockEdge *edge = graph.make_block_edge(declared_source, target);
        declared_source->append_instruction(arena.make_return(none_value()));
        actual_source->append_instruction(
            arena.make_unconditional_branch(edge));
        target->append_instruction(arena.make_return(none_value()));

        expect_invalid_with(graph, "as its source but is referenced by");
    }

    TEST(JitCfgVerifier, RejectsOutgoingEdgeMissingFromPredecessorIndex)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        Block *exit = graph.add_block();
        BlockEdge *edge = arena.make_block_edge(entry, exit);
        entry->append_instruction(arena.make_unconditional_branch(edge));
        exit->append_instruction(arena.make_return(none_value()));

        expect_invalid_with(graph, "times in its target predecessor index");
    }

    TEST(JitCfgVerifier, RejectsPredecessorEdgeMissingFromSourceTerminator)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        Block *exit = graph.add_block();
        graph.make_block_edge(entry, exit);
        entry->append_instruction(arena.make_return(none_value()));
        exit->append_instruction(arena.make_return(none_value()));

        expect_invalid_with(graph, "is not referenced once");
    }

}  // namespace cl::jit
