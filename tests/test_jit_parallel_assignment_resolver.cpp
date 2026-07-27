#include "jit/parallel_assignment_resolver.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace cl::jit
{
    TEST(JitParallelAssignmentResolver, OrdersCycleWithOneScratchLocation)
    {
        std::vector<ParallelAssignment<int>> assignments = {
            {0, 1, RegisterClass::GPR},
            {1, 0, RegisterClass::GPR},
        };

        auto ordered = order_parallel_assignments<int>(
            assignments,
            [](RegisterClass, size_t move_index) -> std::optional<int> {
                EXPECT_EQ(0u, move_index);
                return 2;
            });

        ASSERT_TRUE(ordered);
        ASSERT_EQ(3u, ordered.value().moves.size());
        const OrderedMove<int> &save = ordered.value().moves[0];
        EXPECT_EQ(OrderedMoveSource::Kind::OriginalAssignment,
                  save.source.kind());
        EXPECT_EQ(0u, save.source.index());
        EXPECT_EQ(0, save.source_location);
        EXPECT_EQ(2, save.destination);
        EXPECT_EQ(-1, save.original_assignment_index);

        const OrderedMove<int> &move_rhs = ordered.value().moves[1];
        EXPECT_EQ(OrderedMoveSource::Kind::OriginalAssignment,
                  move_rhs.source.kind());
        EXPECT_EQ(1u, move_rhs.source.index());
        EXPECT_EQ(1, move_rhs.source_location);
        EXPECT_EQ(0, move_rhs.destination);
        EXPECT_EQ(1, move_rhs.original_assignment_index);

        const OrderedMove<int> &move_lhs = ordered.value().moves[2];
        EXPECT_EQ(OrderedMoveSource::Kind::Move, move_lhs.source.kind());
        EXPECT_EQ(0u, move_lhs.source.index());
        EXPECT_EQ(2, move_lhs.source_location);
        EXPECT_EQ(1, move_lhs.destination);
        EXPECT_EQ(0, move_lhs.original_assignment_index);
    }

    TEST(JitParallelAssignmentResolver, ReportsMissingCycleScratch)
    {
        std::vector<ParallelAssignment<int>> assignments = {
            {0, 1, RegisterClass::GPR},
            {1, 0, RegisterClass::GPR},
        };

        auto ordered = order_parallel_assignments<int>(
            assignments, [](RegisterClass, size_t) -> std::optional<int> {
                return std::nullopt;
            });

        ASSERT_TRUE(ordered.has_error());
        EXPECT_EQ(ParallelAssignmentError::InsufficientScratchLocations,
                  ordered.error());
    }

    TEST(JitParallelAssignmentResolver, OrdersLongDependencyChain)
    {
        constexpr int TransferCount = 1024;
        std::vector<ParallelAssignment<int>> assignments;
        assignments.reserve(TransferCount);
        for(int index = 0; index < TransferCount; ++index)
        {
            assignments.push_back({index, index + 1, RegisterClass::GPR});
        }

        auto ordered = order_parallel_assignments<int>(
            assignments, [](RegisterClass, size_t) -> std::optional<int> {
                return std::nullopt;
            });

        ASSERT_TRUE(ordered);
        EXPECT_EQ(TransferCount, ordered.value().moves.size());
    }

    TEST(JitParallelAssignmentResolver, RejectsDuplicateDestinations)
    {
        std::vector<ParallelAssignment<int>> assignments = {
            {0, 2, RegisterClass::GPR},
            {1, 2, RegisterClass::GPR},
        };

        EXPECT_DEATH(
            (void)order_parallel_assignments<int>(
                assignments,
                [](RegisterClass, size_t) -> std::optional<int> { return 3; }),
            "duplicate destination");
    }

}  // namespace cl::jit
