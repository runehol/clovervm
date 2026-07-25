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

        ScratchRegisters scratch_registers()
        {
            ScratchRegisters result;
            result[static_cast<size_t>(RegisterClass::GPR)] = x2;
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
        EXPECT_EQ(x2, save.destination.reg());
        EXPECT_EQ(-1, save.original_parallel_transfer_index);

        const ResolvedTransferStep &move_rhs = resolved.value().steps[1];
        EXPECT_EQ(ResolvedTransferSource::Kind::OriginalTransfer,
                  move_rhs.source.kind());
        EXPECT_EQ(1u, move_rhs.source.index());
        EXPECT_EQ(x0, move_rhs.destination.reg());
        EXPECT_EQ(1, move_rhs.original_parallel_transfer_index);

        const ResolvedTransferStep &move_lhs = resolved.value().steps[2];
        EXPECT_EQ(ResolvedTransferSource::Kind::Step, move_lhs.source.kind());
        EXPECT_EQ(0u, move_lhs.source.index());
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
        EXPECT_EQ(x2, resolved.value().steps[0].destination.reg());
        EXPECT_EQ(-1,
                  resolved.value().steps[0].original_parallel_transfer_index);
        EXPECT_EQ(ResolvedTransferSource::Kind::Step,
                  resolved.value().steps[1].source.kind());
        EXPECT_EQ(-8,
                  resolved.value().steps[1].destination.stack().frame_offset());
        EXPECT_EQ(0,
                  resolved.value().steps[1].original_parallel_transfer_index);
    }

    TEST(JitParallelTransferResolver, ReportsAllStackCycleNeedsSpillSlot)
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

        ASSERT_TRUE(resolved.has_error());
        EXPECT_EQ(RegisterAllocationError::RequiresTransferSpillSlot,
                  resolved.error());
    }

}  // namespace cl::jit
