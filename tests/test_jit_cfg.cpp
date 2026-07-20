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
        class NonTerminatorInstruction final : public Instruction
        {
        public:
            explicit NonTerminatorInstruction(Serial serial)
                : Instruction(serial)
            {
            }
        };

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
        ConditionalBranchInstruction *branch =
            arena.make_instruction<ConditionalBranchInstruction>(true_edge,
                                                                 false_edge);
        entry->append_instruction(branch);
        join->append_instruction(arena.make_instruction<ReturnInstruction>());

        EXPECT_EQ(entry, graph.entry_block());
        ASSERT_EQ(2u, graph.blocks().size());
        EXPECT_EQ(entry, graph.blocks()[0]);
        EXPECT_EQ(join, graph.blocks()[1]);

        EXPECT_EQ(true_edge, branch->true_edge());
        EXPECT_EQ(false_edge, branch->false_edge());
        ASSERT_EQ(2u, branch->block_successor_edges().size());
        EXPECT_EQ(true_edge, branch->block_successor_edges()[0]);
        EXPECT_EQ(false_edge, branch->block_successor_edges()[1]);
        EXPECT_EQ(&branch->block_successor_edges(),
                  &entry->block_successor_edges());

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
        UnconditionalBranchInstruction *branch =
            arena.make_instruction<UnconditionalBranchInstruction>(edge);
        ReturnInstruction *return_instruction =
            arena.make_instruction<ReturnInstruction>();
        entry->append_instruction(branch);
        exit->append_instruction(return_instruction);

        EXPECT_EQ(edge, branch->edge());
        ASSERT_EQ(1u, entry->block_successor_edges().size());
        EXPECT_EQ(edge, entry->block_successor_edges()[0]);
        EXPECT_TRUE(exit->block_successor_edges().empty());
        EXPECT_EQ(TerminatorKind::Return,
                  return_instruction->terminator_kind());
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
        entry->append_instruction(
            arena.make_instruction<NonTerminatorInstruction>());

        expect_invalid_with(graph, "does not end in a block terminator");
    }

    TEST(JitCfgVerifier, RejectsTerminatorBeforeFinalInstruction)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        entry->append_instruction(arena.make_instruction<ReturnInstruction>());
        entry->append_instruction(arena.make_instruction<ReturnInstruction>());

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
        entry->append_instruction(
            arena.make_instruction<ConditionalBranchInstruction>(edge, edge));
        exit->append_instruction(arena.make_instruction<ReturnInstruction>());

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
        declared_source->append_instruction(
            arena.make_instruction<ReturnInstruction>());
        actual_source->append_instruction(
            arena.make_instruction<UnconditionalBranchInstruction>(edge));
        target->append_instruction(arena.make_instruction<ReturnInstruction>());

        expect_invalid_with(graph, "as its source but is referenced by");
    }

    TEST(JitCfgVerifier, RejectsOutgoingEdgeMissingFromPredecessorIndex)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        Block *exit = graph.add_block();
        BlockEdge *edge = arena.make_block_edge(entry, exit);
        entry->append_instruction(
            arena.make_instruction<UnconditionalBranchInstruction>(edge));
        exit->append_instruction(arena.make_instruction<ReturnInstruction>());

        expect_invalid_with(graph, "times in its target predecessor index");
    }

    TEST(JitCfgVerifier, RejectsPredecessorEdgeMissingFromSourceTerminator)
    {
        CompilationArena arena;
        ControlFlowGraph graph(arena);
        Block *entry = graph.add_block();
        Block *exit = graph.add_block();
        graph.make_block_edge(entry, exit);
        entry->append_instruction(arena.make_instruction<ReturnInstruction>());
        exit->append_instruction(arena.make_instruction<ReturnInstruction>());

        expect_invalid_with(graph, "is not referenced once");
    }

}  // namespace cl::jit
