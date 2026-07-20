#include "jit/machine_address_internal.h"
#include "jit/machine_code_emitter.h"
#include "jit_code_cache_test_support.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace cl::jit
{
    namespace
    {
        class TestDirectBranch
        {
        public:
            static constexpr size_t MaximumUnitSize = 64 * 1024;

            TestDirectBranch(Label target, int64_t short_range)
                : target_(target), short_range_(short_range)
            {
            }

            TestDirectBranch(MachineAddress target, int64_t short_range)
                : target_(target), short_range_(short_range)
            {
            }

            const CodeTarget &target() const { return target_; }
            uint32_t min_size() const { return 1; }
            uint32_t max_size() const { return 4; }

            uint32_t select(MachineAddress source, MachineAddress target)
            {
                int64_t displacement = source.displacement_to(target);
                short_selected_ = displacement >= -short_range_ &&
                                  displacement <= short_range_;
                return *short_selected_ ? min_size() : max_size();
            }

            void encode(void *write_pointer, MachineAddress source,
                        MachineAddress target) const
            {
                ASSERT_TRUE(short_selected_.has_value());
                if(*short_selected_)
                {
                    int64_t displacement = source.displacement_to(target);
                    ASSERT_GE(displacement, -short_range_);
                    ASSERT_LE(displacement, short_range_);
                }
                uint32_t size = *short_selected_ ? min_size() : max_size();
                auto *bytes = static_cast<uint8_t *>(write_pointer);
                for(uint32_t index = 0; index < size; ++index)
                {
                    bytes[index] = *short_selected_ ? 0x51 : 0x54;
                }
            }

        private:
            CodeTarget target_;
            int64_t short_range_;
            std::optional<bool> short_selected_;
        };

        struct RelocationObservation
        {
            uintptr_t instruction_pc = 0;
            uintptr_t target = 0;
            void *write_pointer = nullptr;
        };

        class TestRelocation
        {
        public:
            TestRelocation(ValuePoolEntry target,
                           RelocationObservation *observation)
                : target_(target), observation_(observation)
            {
            }

            RelocationTarget target() const { return target_; }
            void apply(void *write_pointer, MachineAddress instruction_pc,
                       MachineAddress target) const
            {
                observation_->write_pointer = write_pointer;
                observation_->instruction_pc =
                    instruction_pc.bits_for_indirect_target();
                observation_->target = target.bits_for_indirect_target();
                *static_cast<uint8_t *>(write_pointer) = 0xcc;
            }

        private:
            ValuePoolEntry target_;
            RelocationObservation *observation_;
        };

        using TestEmitter =
            MachineCodeEmitter<TestDirectBranch, TestRelocation>;

        using test_support::CacheAndPlatform;

        CodeAllocation
        take_allocation(Result<CodeAllocation, JitCodeError> result)
        {
            EXPECT_TRUE(result);
            return std::move(result).value();
        }
    }  // namespace

    TEST(MachineCodeEmitter, ResolvesForwardLabelsAndShrinksDirectBranches)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(64 * 1024);
        Label target = emitter.make_label();
        uint8_t prefix = 0x10;
        uint8_t middle[] = {0x20, 0x21};
        uint8_t suffix = 0x30;

        emitter.emit_bytes(&prefix, sizeof(prefix));
        emitter.emit_direct_branch(TestDirectBranch(target, 8));
        emitter.emit_bytes(middle, sizeof(middle));
        emitter.resolve(target);
        emitter.emit_bytes(&suffix, sizeof(suffix));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        auto *code = static_cast<uint8_t *>(allocation.write_pointer());
        EXPECT_EQ(0x10, code[0]);
        EXPECT_EQ(0x51, code[1]);
        EXPECT_EQ(0x20, code[2]);
        EXPECT_EQ(0x21, code[3]);
        EXPECT_EQ(0x30, code[4]);
    }

    TEST(MachineCodeEmitter, KeepsConservativelyLongForwardDirectBranch)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(64 * 1024);
        Label target = emitter.make_label();

        emitter.emit_direct_branch(TestDirectBranch(target, 5));
        emitter.emit_direct_branch(TestDirectBranch(
            detail::MachineAddressAccess::from_bits(0x10000004), 1));
        emitter.resolve(target);

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        auto *code = static_cast<uint8_t *>(allocation.write_pointer());
        EXPECT_EQ(0x54, code[0]);
        EXPECT_EQ(0x51, code[4]);
    }

    TEST(MachineCodeEmitter, AcceptsLabelResolvedBeforeUse)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(64 * 1024);
        Label target = emitter.make_label();
        emitter.resolve(target);
        emitter.emit_direct_branch(TestDirectBranch(target, 4));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        EXPECT_EQ(0x51, *static_cast<uint8_t *>(allocation.write_pointer()));
    }

    TEST(MachineCodeEmitter, RelocatesValuePoolLoadsUsingExecutableAddresses)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(64 * 1024);
        RelocationObservation observation;
        emitter.add_value_to_constant_pool(Value::None());
        ValuePoolEntry target =
            emitter.add_value_to_constant_pool(Value::True());
        uint8_t instruction = 0;
        emitter.emit_relocatable(&instruction, sizeof(instruction),
                                 TestRelocation(target, &observation));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));

        EXPECT_EQ(0xcc, *static_cast<uint8_t *>(allocation.write_pointer()));
        EXPECT_EQ(allocation.code.execute_address().bits_for_indirect_target(),
                  observation.instruction_pc);
        EXPECT_NE(reinterpret_cast<uintptr_t>(observation.write_pointer),
                  observation.instruction_pc);
        EXPECT_EQ(allocation.value_pool.address()
                      .offset_by(sizeof(Value))
                      .bits_for_indirect_target(),
                  observation.target);
        EXPECT_EQ(Value::None(), allocation.value_pool.write_pointer()[0]);
        EXPECT_EQ(Value::True(), allocation.value_pool.write_pointer()[1]);
    }

    TEST(MachineCodeEmitter, RejectsPoolOutsideRelocationSpanBeforeAllocation)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(8191);
        RelocationObservation observation;
        ValuePoolEntry target =
            emitter.add_value_to_constant_pool(Value::None());
        uint8_t instruction = 0;
        emitter.emit_relocatable(&instruction, sizeof(instruction),
                                 TestRelocation(target, &observation));

        Result<CodeAllocation, JitCodeError> result =
            emitter.finalize(*fixture.cache);

        ASSERT_FALSE(result);
        EXPECT_EQ(JitCodeError::PoolOutOfRange, result.error());
        EXPECT_TRUE(fixture.platform->requested_sizes.empty());
    }

    TEST(MachineCodeEmitter, ReportsAllocationFailure)
    {
        CacheAndPlatform fixture(16);
        fixture.platform->fail_allocation = true;
        TestEmitter emitter(64 * 1024);
        uint8_t instruction = 0;
        emitter.emit_bytes(&instruction, sizeof(instruction));

        Result<CodeAllocation, JitCodeError> result =
            emitter.finalize(*fixture.cache);

        ASSERT_FALSE(result);
        EXPECT_EQ(JitCodeError::AllocationFailure, result.error());
    }

    TEST(MachineCodeEmitter, ReportsCommitFailure)
    {
        CacheAndPlatform fixture(16);
        fixture.platform->fail_commit = true;
        TestEmitter emitter(64 * 1024);
        uint8_t instruction = 0;
        emitter.emit_bytes(&instruction, sizeof(instruction));

        Result<CodeAllocation, JitCodeError> result =
            emitter.finalize(*fixture.cache);

        ASSERT_FALSE(result);
        EXPECT_EQ(JitCodeError::AllocationFailure, result.error());
    }

}  // namespace cl::jit
