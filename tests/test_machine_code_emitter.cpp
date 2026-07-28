#include "jit/machine_address_internal.h"
#include "jit/machine_code_emitter.h"
#include "jit_code_cache_test_support.h"

#include <gtest/gtest.h>

#include <array>
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
            static constexpr uint32_t MinimumSize = 1;
            static constexpr uint32_t MaximumSize = 4;

            TestDirectBranch(Label target, int64_t short_range)
                : target_(target), short_range_(short_range)
            {
            }

            TestDirectBranch(MachineAddress target, int64_t short_range)
                : target_(target), short_range_(short_range)
            {
            }

            const CodeTarget &target() const { return target_; }
            uint32_t min_size() const { return MinimumSize; }
            uint32_t max_size() const { return MaximumSize; }

            uint32_t select(MachineAddress source, MachineAddress target)
            {
                static_assert(MaximumSize <= MaximumUnitSize);
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
            TestRelocation(ConstantPoolEntry target,
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
            ConstantPoolEntry target_;
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

        std::span<Value> pool_values(CodeAllocation &allocation)
        {
            std::span<std::byte> bytes = allocation.constant_pool();
            return {reinterpret_cast<Value *>(bytes.data()),
                    bytes.size() / sizeof(Value)};
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
        auto *code =
            reinterpret_cast<uint8_t *>(allocation.writable_code().data());
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
        auto *code =
            reinterpret_cast<uint8_t *>(allocation.writable_code().data());
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
        EXPECT_EQ(0x51, *reinterpret_cast<uint8_t *>(
                            allocation.writable_code().data()));
    }

    TEST(MachineCodeEmitter, RelocatesValuePoolLoadsUsingExecutableAddresses)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(64 * 1024);
        RelocationObservation observation;
        emitter.add_value_to_constant_pool(Value::None());
        ConstantPoolEntry target =
            emitter.add_value_to_constant_pool(Value::True());
        uint8_t instruction = 0;
        emitter.emit_relocatable(&instruction, sizeof(instruction),
                                 TestRelocation(target, &observation));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));

        EXPECT_EQ(0xcc, *reinterpret_cast<uint8_t *>(
                            allocation.writable_code().data()));
        EXPECT_EQ(allocation.code.execute_address().bits_for_indirect_target(),
                  observation.instruction_pc);
        EXPECT_NE(reinterpret_cast<uintptr_t>(observation.write_pointer),
                  observation.instruction_pc);
        EXPECT_EQ(allocation.constant_pool_address()
                      .offset_by(sizeof(Value))
                      .bits_for_indirect_target(),
                  observation.target);
        EXPECT_EQ(Value::None(), pool_values(allocation)[0]);
        EXPECT_EQ(Value::True(), pool_values(allocation)[1]);
    }

    TEST(MachineCodeEmitter, DeduplicatesValuePoolEntriesByRawValue)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(64 * 1024);
        RelocationObservation first_observation;
        RelocationObservation distinct_observation;
        RelocationObservation duplicate_observation;
        ConstantPoolEntry first =
            emitter.add_value_to_constant_pool(Value::True());
        ConstantPoolEntry distinct =
            emitter.add_value_to_constant_pool(Value::from_smi(1));
        ConstantPoolEntry duplicate =
            emitter.add_value_to_constant_pool(Value::True());
        uint8_t instructions[] = {0, 0, 0};

        emitter.emit_relocatable(&instructions[0], sizeof(instructions[0]),
                                 TestRelocation(first, &first_observation));
        emitter.emit_relocatable(
            &instructions[1], sizeof(instructions[1]),
            TestRelocation(distinct, &distinct_observation));
        emitter.emit_relocatable(
            &instructions[2], sizeof(instructions[2]),
            TestRelocation(duplicate, &duplicate_observation));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        uintptr_t pool_address =
            allocation.constant_pool_address().bits_for_indirect_target();

        EXPECT_EQ(2u, pool_values(allocation).size());
        EXPECT_EQ(pool_address, first_observation.target);
        EXPECT_EQ(pool_address + sizeof(Value), distinct_observation.target);
        EXPECT_EQ(first_observation.target, duplicate_observation.target);
        EXPECT_EQ(Value::True(), pool_values(allocation)[0]);
        EXPECT_EQ(Value::from_smi(1), pool_values(allocation)[1]);
    }

    TEST(MachineCodeEmitter,
         KeepsInterleavedTaggedAndUntaggedEntriesInStablePoolAreas)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(64 * 1024);
        std::array<std::byte, 3> first_data = {std::byte{0x11}, std::byte{0x12},
                                               std::byte{0x13}};
        std::array<std::byte, 2> second_data = {std::byte{0x21},
                                                std::byte{0x22}};

        ConstantPoolEntry first = emitter.add_data_to_constant_pool(first_data);
        ConstantPoolEntry none =
            emitter.add_value_to_constant_pool(Value::None());
        ConstantPoolEntry second =
            emitter.add_data_to_constant_pool(second_data);
        ConstantPoolEntry truth =
            emitter.add_value_to_constant_pool(Value::True());

        std::array<RelocationObservation, 4> observations;
        uint8_t instructions[] = {0, 0, 0, 0};
        emitter.emit_relocatable(&instructions[0], sizeof(instructions[0]),
                                 TestRelocation(first, &observations[0]));
        emitter.emit_relocatable(&instructions[1], sizeof(instructions[1]),
                                 TestRelocation(none, &observations[1]));
        emitter.emit_relocatable(&instructions[2], sizeof(instructions[2]),
                                 TestRelocation(second, &observations[2]));
        emitter.emit_relocatable(&instructions[3], sizeof(instructions[3]),
                                 TestRelocation(truth, &observations[3]));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        std::span<std::byte> pool = allocation.constant_pool();
        uintptr_t pool_address =
            allocation.constant_pool_address().bits_for_indirect_target();

        EXPECT_EQ(2u, emitter.tagged_value_count());
        EXPECT_EQ(26u, pool.size());
        EXPECT_EQ(Value::None(), pool_values(allocation)[0]);
        EXPECT_EQ(Value::True(), pool_values(allocation)[1]);
        EXPECT_EQ(pool_address + 16, observations[0].target);
        EXPECT_EQ(pool_address, observations[1].target);
        EXPECT_EQ(pool_address + 24, observations[2].target);
        EXPECT_EQ(pool_address + 8, observations[3].target);
        EXPECT_EQ(std::byte{0x11}, pool[16]);
        EXPECT_EQ(std::byte{0x12}, pool[17]);
        EXPECT_EQ(std::byte{0x13}, pool[18]);
        for(size_t index = 19; index < 24; ++index)
        {
            EXPECT_EQ(std::byte{0}, pool[index]);
        }
        EXPECT_EQ(std::byte{0x21}, pool[24]);
        EXPECT_EQ(std::byte{0x22}, pool[25]);
    }

    TEST(MachineCodeEmitter, RejectsPoolOutsideRelocationSpanBeforeAllocation)
    {
        CacheAndPlatform fixture(16);
        TestEmitter emitter(8191);
        RelocationObservation observation;
        ConstantPoolEntry target =
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
