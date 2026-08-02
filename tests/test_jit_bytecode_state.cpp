#include "jit/bytecode_state.h"

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cl::jit
{
    namespace
    {
        using TestRef = uint32_t;

        struct StateFixture
        {
            StateFixture()
            {
                code_object = context.compile_file(L"pass\n");
                code_object->function_signature.n_parameters = 3;
                code_object->n_locals = 2;
                code_object->n_temporaries = 3;
            }

            test::VmTestContext context;
            CodeObject *code_object = nullptr;
        };

        BytecodeValueLocation location(const CodeObject &code_object,
                                       uint32_t register_index)
        {
            return BytecodeValueLocation::stack_slot(
                code_object.encode_reg(register_index));
        }

        BytecodeValueLocation parameter(const CodeObject &code_object,
                                        uint32_t index)
        {
            return location(code_object, index);
        }

        BytecodeValueLocation local(const CodeObject &code_object,
                                    uint32_t index)
        {
            return location(code_object, code_object.get_padded_n_parameters() +
                                             FrameHeaderSize + index);
        }

        BytecodeValueLocation temporary(const CodeObject &code_object,
                                        uint32_t index)
        {
            return location(code_object, code_object.get_padded_n_parameters() +
                                             FrameHeaderSize +
                                             code_object.n_locals + index);
        }

        BytecodeState<TestRef>
        make_entry_state(const BytecodeStateTracker<TestRef> &tracker)
        {
            std::array<TestRef, 3> parameters = {10, 11, 12};
            std::array<TestRef, FrameHeaderSize> frame_header = {20, 21, 22,
                                                                 23};
            return tracker.make_entry_state(
                std::span<const TestRef>(parameters),
                std::span<const TestRef>(frame_header), 90, 99);
        }
    }  // namespace

    TEST(JitBytecodeState, InitializesEverySemanticEntryLocation)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        BytecodeState<TestRef> state = make_entry_state(tracker);

        EXPECT_EQ(
            99u, tracker.value_at(state, BytecodeValueLocation::accumulator()));
        EXPECT_EQ(10u,
                  tracker.value_at(state, parameter(*fixture.code_object, 0)));
        EXPECT_EQ(12u,
                  tracker.value_at(state, parameter(*fixture.code_object, 2)));
        EXPECT_EQ(90u, tracker.value_at(state, local(*fixture.code_object, 0)));
        EXPECT_EQ(90u, tracker.value_at(state, local(*fixture.code_object, 1)));
        EXPECT_EQ(99u,
                  tracker.value_at(state, temporary(*fixture.code_object, 0)));
        EXPECT_EQ(99u,
                  tracker.value_at(state, temporary(*fixture.code_object, 2)));

        std::span<const TestRef> values = tracker.values(state);
        EXPECT_EQ(99u, values[4]);
        EXPECT_EQ(23u, values[5]);
        EXPECT_EQ(20u, values[8]);
    }

    TEST(JitBytecodeState, UsesCanonicalDescendingStackOrder)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        const BytecodeStateOrder &order = tracker.order();

        EXPECT_EQ(14u, order.size());
        EXPECT_EQ(fixture.code_object->encode_reg(0),
                  order.highest_frame_offset());
        EXPECT_EQ(-5, order.lowest_frame_offset());
        EXPECT_EQ(7, order.frame_offset_at(1));
        EXPECT_EQ(6, order.frame_offset_at(2));
        EXPECT_EQ(-5, order.frame_offset_at(13));

        EXPECT_EQ(0u, order.position_for(BytecodeValueLocation::accumulator()));
        EXPECT_EQ(1u, order.position_for(parameter(*fixture.code_object, 0)));
        EXPECT_EQ(3u, order.position_for(parameter(*fixture.code_object, 2)));
        EXPECT_EQ(4u, order.position_for_frame_offset(4));
        EXPECT_EQ(5u,
                  order.position_for_frame_offset(FrameHeaderReturnPcOffset));
        EXPECT_EQ(8u,
                  order.position_for_frame_offset(FrameHeaderPreviousFpOffset));
        EXPECT_EQ(9u, order.position_for(local(*fixture.code_object, 0)));
        EXPECT_EQ(11u, order.position_for(temporary(*fixture.code_object, 0)));
    }

    TEST(JitBytecodeState, ZeroParameterOrderStartsAtHighestFrameHeaderSlot)
    {
        StateFixture fixture;
        fixture.code_object->function_signature.n_parameters = 0;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        const BytecodeStateOrder &order = tracker.order();

        EXPECT_EQ(FrameHeaderReturnPcOffset, order.highest_frame_offset());
        EXPECT_EQ(10u, order.size());
        EXPECT_EQ(FrameHeaderReturnPcOffset, order.frame_offset_at(1));
        EXPECT_EQ(FrameHeaderPreviousFpOffset, order.frame_offset_at(4));
        EXPECT_EQ(-1, order.frame_offset_at(5));
    }

    TEST(JitBytecodeState, ReadsSourcesBeforeApplyingMultipleResults)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        BytecodeState<TestRef> state = make_entry_state(tracker);

        std::array<BytecodeValueLocation, 3> sources = {
            BytecodeValueLocation::accumulator(),
            parameter(*fixture.code_object, 1), local(*fixture.code_object, 0)};
        std::vector<TestRef> inputs = tracker.read(
            state, std::span<const BytecodeValueLocation>(sources));
        EXPECT_EQ((std::vector<TestRef>{99, 11, 90}), inputs);

        std::array<BytecodeValueLocation, 3> destinations = {
            parameter(*fixture.code_object, 1), local(*fixture.code_object, 0),
            BytecodeValueLocation::accumulator()};
        std::array<TestRef, 3> results = {101, 102, 103};
        tracker.write(state,
                      std::span<const BytecodeValueLocation>(destinations),
                      std::span<const TestRef>(results));

        EXPECT_EQ(101u,
                  tracker.value_at(state, parameter(*fixture.code_object, 1)));
        EXPECT_EQ(102u,
                  tracker.value_at(state, local(*fixture.code_object, 0)));
        EXPECT_EQ(103u, tracker.value_at(state,
                                         BytecodeValueLocation::accumulator()));
    }

    TEST(JitBytecodeState, PreservesReferenceIdentityForAliasingUpdates)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        BytecodeState<TestRef> state = make_entry_state(tracker);

        std::array<BytecodeValueLocation, 1> source = {
            parameter(*fixture.code_object, 0)};
        std::vector<TestRef> result =
            tracker.read(state, std::span<const BytecodeValueLocation>(source));
        std::array<BytecodeValueLocation, 1> destination = {
            BytecodeValueLocation::accumulator()};
        tracker.write(state,
                      std::span<const BytecodeValueLocation>(destination),
                      std::span<const TestRef>(result));

        EXPECT_EQ(
            10u, tracker.value_at(state, BytecodeValueLocation::accumulator()));
    }

    TEST(JitBytecodeState, ReplacesEveryOccurrenceOfAValue)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        BytecodeState<TestRef> state = make_entry_state(tracker);

        tracker.replace_value(state, 99, 77);

        EXPECT_EQ(
            77u, tracker.value_at(state, BytecodeValueLocation::accumulator()));
        EXPECT_EQ(77u,
                  tracker.value_at(state, temporary(*fixture.code_object, 0)));
        EXPECT_EQ(77u,
                  tracker.value_at(state, temporary(*fixture.code_object, 2)));
        EXPECT_EQ(10u,
                  tracker.value_at(state, parameter(*fixture.code_object, 0)));
    }

    TEST(JitBytecodeState, CopiesStatesIndependently)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        BytecodeState<TestRef> original = make_entry_state(tracker);
        BytecodeState<TestRef> copy = original;

        std::array<BytecodeValueLocation, 1> destination = {
            temporary(*fixture.code_object, 1)};
        std::array<TestRef, 1> result = {77};
        tracker.write(copy, std::span<const BytecodeValueLocation>(destination),
                      std::span<const TestRef>(result));

        EXPECT_EQ(99u, tracker.value_at(original,
                                        temporary(*fixture.code_object, 1)));
        EXPECT_EQ(77u,
                  tracker.value_at(copy, temporary(*fixture.code_object, 1)));
    }

    TEST(JitBytecodeState, RoundTripsBlockTransferOrder)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        std::array<TestRef, 14> parameters = {1, 2, 3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14};

        BytecodeState<TestRef> state = tracker.make_state_from_block_parameters(
            std::span<const TestRef>(parameters));
        std::span<const TestRef> arguments = tracker.block_arguments(state);

        EXPECT_EQ((std::vector<TestRef>(parameters.begin(), parameters.end())),
                  (std::vector<TestRef>(arguments.begin(), arguments.end())));
        EXPECT_EQ(
            1u, tracker.value_at(state, BytecodeValueLocation::accumulator()));
        EXPECT_EQ(4u,
                  tracker.value_at(state, parameter(*fixture.code_object, 2)));
        EXPECT_EQ(11u, tracker.value_at(state, local(*fixture.code_object, 1)));
        EXPECT_EQ(14u,
                  tracker.value_at(state, temporary(*fixture.code_object, 2)));
    }

    TEST(JitBytecodeState, ExportsOnlyCanonicalPrefixes)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        std::array<TestRef, 14> parameters = {1, 2, 3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14};
        BytecodeState<TestRef> state = tracker.make_state_from_block_parameters(
            std::span<const TestRef>(parameters));

        std::span<const TestRef> prefix = tracker.prefix(state, 8);
        EXPECT_EQ((std::vector<TestRef>{1, 2, 3, 4, 5, 6, 7, 8}),
                  (std::vector<TestRef>(prefix.begin(), prefix.end())));
    }

    TEST(JitBytecodeState, RejectsInvalidLocationsAndArities)
    {
        EXPECT_DEATH(
            ([] {
                StateFixture fixture;
                BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
                std::array<TestRef, 2> parameters = {1, 2};
                std::array<TestRef, FrameHeaderSize> frame_header = {20, 21, 22,
                                                                     23};
                (void)tracker.make_entry_state(
                    std::span<const TestRef>(parameters),
                    std::span<const TestRef>(frame_header), 90, 99);
            }()),
            "wrong parameter count");

        EXPECT_DEATH(([] {
                         StateFixture fixture;
                         BytecodeStateTracker<TestRef> tracker(
                             *fixture.code_object);
                         std::array<TestRef, 3> parameters = {1, 2, 3};
                         std::array<TestRef, 3> frame_header = {20, 21, 22};
                         (void)tracker.make_entry_state(
                             std::span<const TestRef>(parameters),
                             std::span<const TestRef>(frame_header), 90, 99);
                     }()),
                     "wrong frame-header size");

        EXPECT_DEATH(
            ([] {
                StateFixture fixture;
                BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
                BytecodeState<TestRef> state = make_entry_state(tracker);
                (void)tracker.value_at(state,
                                       BytecodeValueLocation::stack_slot(-6));
            }()),
            "frame offset is outside the state order");

        EXPECT_DEATH(
            ([] {
                StateFixture fixture;
                BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
                BytecodeState<TestRef> state = make_entry_state(tracker);
                std::array<BytecodeValueLocation, 1> destinations = {
                    parameter(*fixture.code_object, 0)};
                std::span<const TestRef> no_results;
                tracker.write(
                    state, std::span<const BytecodeValueLocation>(destinations),
                    no_results);
            }()),
            "mismatched destination and result counts");

        EXPECT_DEATH(
            ([] {
                StateFixture fixture;
                BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
                std::array<TestRef, 9> parameters = {1, 2, 3, 4, 5, 6, 7, 8, 9};
                (void)tracker.make_state_from_block_parameters(
                    std::span<const TestRef>(parameters));
            }()),
            "block state has the wrong parameter count");
    }

}  // namespace cl::jit
