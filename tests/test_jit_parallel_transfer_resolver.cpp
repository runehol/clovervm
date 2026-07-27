#include "jit/parallel_transfer_resolver.h"

#include <gtest/gtest.h>

#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr PhysicalRegister x0(RegisterClass::GPR, 0);
        constexpr PhysicalRegister x1(RegisterClass::GPR, 1);
        constexpr PhysicalRegister x2(RegisterClass::GPR, 2);
        constexpr PhysicalRegister x3(RegisterClass::GPR, 3);

        ScratchRegisters scratch_registers(bool include_second = true)
        {
            ScratchRegisters result;
            result[static_cast<size_t>(RegisterClass::GPR)].push_back(x2);
            if(include_second)
            {
                result[static_cast<size_t>(RegisterClass::GPR)].push_back(x3);
            }
            return result;
        }
    }  // namespace

    TEST(JitParallelTransferResolver, BreaksRegisterCycleWithScratch)
    {
        std::vector<ParallelTransfer> transfers = {
            {PhysicalLocation::reg(x0), PhysicalLocation::reg(x1),
             RegisterClass::GPR},
            {PhysicalLocation::reg(x1), PhysicalLocation::reg(x0),
             RegisterClass::GPR},
        };

        auto resolved =
            resolve_parallel_transfers(transfers, scratch_registers());

        ASSERT_TRUE(resolved);
        ASSERT_EQ(3u, resolved.value().steps.size());
        const ResolvedTransferStep &save = resolved.value().steps[0];
        EXPECT_EQ(ResolvedTransferSource::Kind::OriginalTransfer,
                  save.source.kind());
        EXPECT_EQ(0u, save.source.index());
        EXPECT_EQ(x0, save.source_location.reg());
        EXPECT_EQ(x2, save.destination.reg());
        EXPECT_EQ(-1, save.original_parallel_transfer_index);

        const ResolvedTransferStep &move_rhs = resolved.value().steps[1];
        EXPECT_EQ(ResolvedTransferSource::Kind::OriginalTransfer,
                  move_rhs.source.kind());
        EXPECT_EQ(1u, move_rhs.source.index());
        EXPECT_EQ(x1, move_rhs.source_location.reg());
        EXPECT_EQ(x0, move_rhs.destination.reg());
        EXPECT_EQ(1, move_rhs.original_parallel_transfer_index);

        const ResolvedTransferStep &move_lhs = resolved.value().steps[2];
        EXPECT_EQ(ResolvedTransferSource::Kind::Step, move_lhs.source.kind());
        EXPECT_EQ(0u, move_lhs.source.index());
        EXPECT_EQ(x2, move_lhs.source_location.reg());
        EXPECT_EQ(x1, move_lhs.destination.reg());
        EXPECT_EQ(0, move_lhs.original_parallel_transfer_index);
    }

    TEST(JitParallelTransferResolver, RoutesStackTransferThroughScratch)
    {
        StackLocation source(StackLocationKind::IncomingParameter, 8);
        StackLocation destination(StackLocationKind::LocalOrTemporary, -8);
        std::vector<ParallelTransfer> transfers = {
            {PhysicalLocation::stack(source),
             PhysicalLocation::stack(destination), RegisterClass::GPR},
        };

        auto resolved =
            resolve_parallel_transfers(transfers, scratch_registers());

        ASSERT_TRUE(resolved);
        ASSERT_EQ(2u, resolved.value().steps.size());
        EXPECT_EQ(
            8,
            resolved.value().steps[0].source_location.stack().frame_offset());
        EXPECT_EQ(x2, resolved.value().steps[0].destination.reg());
        EXPECT_EQ(-1,
                  resolved.value().steps[0].original_parallel_transfer_index);
        EXPECT_EQ(ResolvedTransferSource::Kind::Step,
                  resolved.value().steps[1].source.kind());
        EXPECT_EQ(x2, resolved.value().steps[1].source_location.reg());
        EXPECT_EQ(-8,
                  resolved.value().steps[1].destination.stack().frame_offset());
        EXPECT_EQ(0,
                  resolved.value().steps[1].original_parallel_transfer_index);
    }

    TEST(JitParallelTransferResolver,
         ResolvesAllStackCycleWithTwoScratchRegisters)
    {
        StackLocation first(StackLocationKind::LocalOrTemporary, -8);
        StackLocation second(StackLocationKind::LocalOrTemporary, -16);
        std::vector<ParallelTransfer> transfers = {
            {PhysicalLocation::stack(first), PhysicalLocation::stack(second),
             RegisterClass::GPR},
            {PhysicalLocation::stack(second), PhysicalLocation::stack(first),
             RegisterClass::GPR},
        };

        auto resolved =
            resolve_parallel_transfers(transfers, scratch_registers());

        ASSERT_TRUE(resolved);
        ASSERT_EQ(4u, resolved.value().steps.size());
        EXPECT_EQ(x2, resolved.value().steps[0].destination.reg());
        EXPECT_EQ(-1,
                  resolved.value().steps[0].original_parallel_transfer_index);
        EXPECT_EQ(x3, resolved.value().steps[1].destination.reg());
        EXPECT_EQ(-1,
                  resolved.value().steps[1].original_parallel_transfer_index);
        EXPECT_EQ(x3, resolved.value().steps[2].source_location.reg());
        EXPECT_EQ(-8,
                  resolved.value().steps[2].destination.stack().frame_offset());
        EXPECT_EQ(1,
                  resolved.value().steps[2].original_parallel_transfer_index);
        EXPECT_EQ(x2, resolved.value().steps[3].source_location.reg());
        EXPECT_EQ(-16,
                  resolved.value().steps[3].destination.stack().frame_offset());
        EXPECT_EQ(0,
                  resolved.value().steps[3].original_parallel_transfer_index);
    }

    TEST(JitParallelTransferResolver,
         ResolvesThreeLocationAllStackCycleWithTwoScratchRegisters)
    {
        StackLocation first(StackLocationKind::LocalOrTemporary, -8);
        StackLocation second(StackLocationKind::LocalOrTemporary, -16);
        StackLocation third(StackLocationKind::LocalOrTemporary, -24);
        std::vector<ParallelTransfer> transfers = {
            {PhysicalLocation::stack(first), PhysicalLocation::stack(second),
             RegisterClass::GPR},
            {PhysicalLocation::stack(second), PhysicalLocation::stack(third),
             RegisterClass::GPR},
            {PhysicalLocation::stack(third), PhysicalLocation::stack(first),
             RegisterClass::GPR},
        };

        auto resolved =
            resolve_parallel_transfers(transfers, scratch_registers());

        ASSERT_TRUE(resolved);
        ASSERT_EQ(6u, resolved.value().steps.size());
        EXPECT_EQ(x2, resolved.value().steps[0].destination.reg());
        EXPECT_EQ(x3, resolved.value().steps[1].destination.reg());
        EXPECT_EQ(-8,
                  resolved.value().steps[2].destination.stack().frame_offset());
        EXPECT_EQ(x3, resolved.value().steps[3].destination.reg());
        EXPECT_EQ(-24,
                  resolved.value().steps[4].destination.stack().frame_offset());
        EXPECT_EQ(-16,
                  resolved.value().steps[5].destination.stack().frame_offset());
    }

    TEST(JitParallelTransferResolver,
         ReportsAllStackCycleRequiresSecondScratchRegister)
    {
        StackLocation first(StackLocationKind::LocalOrTemporary, -8);
        StackLocation second(StackLocationKind::LocalOrTemporary, -16);
        std::vector<ParallelTransfer> transfers = {
            {PhysicalLocation::stack(first), PhysicalLocation::stack(second),
             RegisterClass::GPR},
            {PhysicalLocation::stack(second), PhysicalLocation::stack(first),
             RegisterClass::GPR},
        };

        auto resolved =
            resolve_parallel_transfers(transfers, scratch_registers(false));

        ASSERT_TRUE(resolved.has_error());
        EXPECT_EQ(RegisterAllocationError::InsufficientTransferScratchRegisters,
                  resolved.error());
    }

}  // namespace cl::jit
