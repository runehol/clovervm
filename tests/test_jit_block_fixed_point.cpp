#include "jit/block_fixed_point.h"
#include "jit/compilation_session.h"
#include "jit/deduplicating_queue.h"
#include "jit/graph_builder.h"
#include "object_model/value.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <vector>

namespace cl::jit
{
    namespace
    {
        TaggedValueRef emplace_true(GraphBuilder &builder, Block *block)
        {
            return TaggedValueRef(builder.emplace_instruction<ConstInstruction>(
                block, Value::True()));
        }

        void emplace_return(GraphBuilder &builder, Block *block)
        {
            builder.emplace_instruction<BareReturnInstruction>(
                block, emplace_true(builder, block));
        }
    }  // namespace

    TEST(JitDeduplicatingQueue, PreservesFifoOrderAndAllowsRequeue)
    {
        DeduplicatingQueue<int> queue;
        EXPECT_TRUE(queue.empty());
        EXPECT_TRUE(queue.enqueue(1));
        EXPECT_TRUE(queue.enqueue(2));
        EXPECT_FALSE(queue.enqueue(1));
        EXPECT_EQ(2u, queue.size());

        EXPECT_EQ(1, queue.dequeue());
        EXPECT_TRUE(queue.enqueue(1));
        EXPECT_EQ(2, queue.dequeue());
        EXPECT_EQ(1, queue.dequeue());
        EXPECT_TRUE(queue.empty());
    }

    TEST(JitBlockFixedPoint,
         ForwardInitialVisitQueuesVisitedDependantsOnceInEdgeOrder)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *first = builder.emplace_block();
        Block *second = builder.emplace_block();
        Block *duplicate_exception = builder.emplace_block();
        Block *ordered_exception = builder.emplace_block();

        builder.register_exception_entry_block(duplicate_exception);
        builder.register_exception_entry_block(ordered_exception);

        BlockEdge *entry_first = builder.make_block_edge(entry, first);
        BlockEdge *entry_second = builder.make_block_edge(entry, second);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, emplace_true(builder, entry), entry_first, entry_second);
        emplace_return(builder, first);
        emplace_return(builder, second);

        BlockEdge *duplicate_first =
            builder.make_block_edge(duplicate_exception, first);
        BlockEdge *duplicate_second =
            builder.make_block_edge(duplicate_exception, first);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            duplicate_exception, emplace_true(builder, duplicate_exception),
            duplicate_first, duplicate_second);

        BlockEdge *ordered_first =
            builder.make_block_edge(ordered_exception, first);
        BlockEdge *ordered_second =
            builder.make_block_edge(ordered_exception, second);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            ordered_exception, emplace_true(builder, ordered_exception),
            ordered_first, ordered_second);

        ControlFlowGraph *graph = builder.finalize();
        std::vector<const Block *> visited;
        FixedPointStatus status = iterate_blocks_to_fixed_point(
            *graph, BlockOrder::Forward, 2, [&](const Block &block) {
                visited.push_back(&block);
                return &block == duplicate_exception ||
                               &block == ordered_exception
                           ? DataflowUpdate::Changed
                           : DataflowUpdate::Unchanged;
            });

        EXPECT_EQ(FixedPointStatus::Converged, status);
        EXPECT_EQ((std::vector<const Block *>{
                      entry, second, first, duplicate_exception,
                      ordered_exception, first, second}),
                  visited);
    }

    TEST(JitBlockFixedPoint, BackwardChangeRevisitsVisitedPredecessor)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();
        Block *body = builder.emplace_block();
        Block *exit = builder.emplace_block();

        BlockEdge *entry_loop = builder.make_block_edge(entry, loop);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    entry_loop);
        BlockEdge *loop_body = builder.make_block_edge(loop, body);
        BlockEdge *loop_exit = builder.make_block_edge(loop, exit);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            loop, emplace_true(builder, loop), loop_body, loop_exit);
        BlockEdge *body_loop = builder.make_block_edge(body, loop);
        builder.emplace_instruction<UnconditionalBranchInstruction>(body,
                                                                    body_loop);
        emplace_return(builder, exit);

        ControlFlowGraph *graph = builder.finalize();
        std::vector<const Block *> visited;
        bool changed_loop = false;
        FixedPointStatus status = iterate_blocks_to_fixed_point(
            *graph, BlockOrder::Backward, 1, [&](const Block &block) {
                visited.push_back(&block);
                if(&block == loop && !changed_loop)
                {
                    changed_loop = true;
                    return DataflowUpdate::Changed;
                }
                return DataflowUpdate::Unchanged;
            });

        EXPECT_EQ(FixedPointStatus::Converged, status);
        ASSERT_EQ(graph->blocks().size() + 1, visited.size());
        EXPECT_EQ(body, visited.back());
    }

    TEST(JitBlockFixedPoint, SelfRequeueConvergesAtExactRevisitLimit)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *loop = builder.emplace_block();
        BlockEdge *backedge = builder.make_block_edge(loop, loop);
        builder.emplace_instruction<UnconditionalBranchInstruction>(loop,
                                                                    backedge);
        ControlFlowGraph *graph = builder.finalize();

        size_t visits = 0;
        FixedPointStatus status = iterate_blocks_to_fixed_point(
            *graph, BlockOrder::Forward, 2, [&](const Block &) {
                ++visits;
                return visits < 3 ? DataflowUpdate::Changed
                                  : DataflowUpdate::Unchanged;
            });

        EXPECT_EQ(FixedPointStatus::Converged, status);
        EXPECT_EQ(3u, visits);
    }

    TEST(JitBlockFixedPoint, RevisitLimitStillPerformsCompleteInitialVisit)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();
        BlockEdge *entry_loop = builder.make_block_edge(entry, loop);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    entry_loop);
        BlockEdge *backedge = builder.make_block_edge(loop, loop);
        builder.emplace_instruction<UnconditionalBranchInstruction>(loop,
                                                                    backedge);
        ControlFlowGraph *graph = builder.finalize();

        size_t visits = 0;
        FixedPointStatus status = iterate_blocks_to_fixed_point(
            *graph, BlockOrder::Forward, 0, [&](const Block &) {
                ++visits;
                return DataflowUpdate::Changed;
            });

        EXPECT_EQ(FixedPointStatus::RevisitLimitReached, status);
        EXPECT_EQ(2u, visits);
    }

}  // namespace cl::jit
