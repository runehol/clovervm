#include "jit/block_order.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/instruction_traversal.h"
#include "object_model/value.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace cl::jit
{
    namespace
    {
        TaggedValueRef emplace_none(GraphBuilder &builder, Block *block)
        {
            return TaggedValueRef(builder.emplace_instruction<ConstInstruction>(
                block, Value::None()));
        }
    }  // namespace

    TEST(JitBlockOrder, OrdersCompleteGraphStably)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *left = builder.emplace_block();
        Block *right = builder.emplace_block();
        Block *join = builder.emplace_block();
        Block *loop = builder.emplace_block();
        Block *first_exception = builder.emplace_block();
        Block *ordinary_disconnected = builder.emplace_block();
        Block *second_exception = builder.emplace_block();

        builder.register_exception_entry_block(first_exception);
        builder.register_exception_entry_block(second_exception);

        BlockEdge *entry_left = builder.make_block_edge(entry, left);
        BlockEdge *entry_right = builder.make_block_edge(entry, right);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, emplace_none(builder, entry), entry_left, entry_right);

        BlockEdge *left_join = builder.make_block_edge(left, join);
        builder.emplace_instruction<UnconditionalBranchInstruction>(left,
                                                                    left_join);

        BlockEdge *right_join_first = builder.make_block_edge(right, join);
        BlockEdge *right_join_second = builder.make_block_edge(right, join);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            right, emplace_none(builder, right), right_join_first,
            right_join_second);

        BlockEdge *join_loop = builder.make_block_edge(join, loop);
        builder.emplace_instruction<UnconditionalBranchInstruction>(join,
                                                                    join_loop);

        BlockEdge *loop_backedge = builder.make_block_edge(loop, loop);
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            loop, loop_backedge);

        BlockEdge *exception_join =
            builder.make_block_edge(first_exception, join);
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            first_exception, exception_join);

        builder.emplace_instruction<BareReturnInstruction>(
            ordinary_disconnected,
            emplace_none(builder, ordinary_disconnected));
        builder.emplace_instruction<BareReturnInstruction>(
            second_exception, emplace_none(builder, second_exception));

        ControlFlowGraph *graph = builder.finalize();

        EXPECT_EQ((std::vector<const Block *>{
                      entry, left, right, join, loop, first_exception,
                      ordinary_disconnected, second_exception}),
                  ordered_blocks(*graph, BlockOrder::Program));

        std::vector<const Block *> forward =
            ordered_blocks(*graph, BlockOrder::Forward);
        EXPECT_EQ((std::vector<const Block *>{entry, right, left, join, loop,
                                              first_exception, second_exception,
                                              ordinary_disconnected}),
                  forward);
        EXPECT_EQ(graph->blocks().size(), forward.size());

        std::vector<const Block *> backward =
            ordered_blocks(*graph, BlockOrder::Backward);
        std::reverse(forward.begin(), forward.end());
        EXPECT_EQ(forward, backward);
    }

    TEST(JitBlockOrder,
         InstructionTraversalUsesBlockOrderAndForwardInstructionOrder)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *first = builder.emplace_block();
        Block *second = builder.emplace_block();

        BlockEdge *first_edge = builder.make_block_edge(entry, first);
        BlockEdge *second_edge = builder.make_block_edge(entry, second);
        ConstInstruction condition =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(condition), first_edge, second_edge);

        ConstInstruction first_value =
            builder.emplace_instruction<ConstInstruction>(first, Value::None());
        builder.emplace_instruction<BareReturnInstruction>(
            first, TaggedValueRef(first_value));
        ConstInstruction second_value =
            builder.emplace_instruction<ConstInstruction>(second,
                                                          Value::False());
        builder.emplace_instruction<BareReturnInstruction>(
            second, TaggedValueRef(second_value));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<const Block *> visited_blocks;
        std::vector<Instruction> visited_instructions;
        walk_instructions(
            *graph,
            InstructionTraversal().with_block_order(BlockOrder::Forward),
            [&](const GraphQueries &, const Block &block,
                Instruction instruction) {
                visited_blocks.push_back(&block);
                visited_instructions.push_back(instruction);
            });

        ASSERT_EQ(6u, visited_blocks.size());
        EXPECT_EQ((std::vector<const Block *>{entry, entry, second, second,
                                              first, first}),
                  visited_blocks);
        EXPECT_EQ(condition, visited_instructions[0]);
        EXPECT_EQ(second_value, visited_instructions[2]);
        EXPECT_EQ(first_value, visited_instructions[4]);
    }

}  // namespace cl::jit
