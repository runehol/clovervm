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

        BytecodeValueLocation parameter(uint32_t index)
        {
            return {BytecodeValueLocationKind::Parameter, index};
        }

        BytecodeValueLocation local(const CodeObject &code_object,
                                    uint32_t index)
        {
            return {BytecodeValueLocationKind::Local,
                    code_object.get_padded_n_parameters() + FrameHeaderSize +
                        index};
        }

        BytecodeValueLocation temporary(const CodeObject &code_object,
                                        uint32_t index)
        {
            return {
                BytecodeValueLocationKind::Temporary,
                code_object.get_padded_n_parameters() + FrameHeaderSize +
                    code_object.n_locals + index,
            };
        }

        BytecodeState<TestRef>
        make_entry_state(const BytecodeStateTracker<TestRef> &tracker)
        {
            std::array<TestRef, 3> parameters = {10, 11, 12};
            return tracker.make_entry_state(
                std::span<const TestRef>(parameters), 90, 99);
        }
    }  // namespace

    TEST(JitBytecodeState, InitializesEverySemanticEntryLocation)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        BytecodeState<TestRef> state = make_entry_state(tracker);

        EXPECT_EQ(
            99u, tracker.value_at(state, BytecodeValueLocation::accumulator()));
        EXPECT_EQ(10u, tracker.value_at(state, parameter(0)));
        EXPECT_EQ(12u, tracker.value_at(state, parameter(2)));
        EXPECT_EQ(90u, tracker.value_at(state, local(*fixture.code_object, 0)));
        EXPECT_EQ(90u, tracker.value_at(state, local(*fixture.code_object, 1)));
        EXPECT_EQ(99u,
                  tracker.value_at(state, temporary(*fixture.code_object, 0)));
        EXPECT_EQ(99u,
                  tracker.value_at(state, temporary(*fixture.code_object, 2)));
    }

    TEST(JitBytecodeState, UsesRawDecodedRegisterCoordinates)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);

        std::span<const BytecodeValueLocation> locations =
            tracker.block_transfer_locations();
        ASSERT_EQ(9u, locations.size());
        EXPECT_EQ(BytecodeValueLocationKind::Accumulator, locations[0].kind);
        EXPECT_EQ(0u, locations[0].register_index);
        EXPECT_EQ(BytecodeValueLocationKind::Parameter, locations[3].kind);
        EXPECT_EQ(2u, locations[3].register_index);
        EXPECT_EQ(BytecodeValueLocationKind::Local, locations[4].kind);
        EXPECT_EQ(fixture.code_object->get_padded_n_parameters() +
                      FrameHeaderSize,
                  locations[4].register_index);
        EXPECT_EQ(BytecodeValueLocationKind::Temporary, locations[6].kind);
        EXPECT_EQ(fixture.code_object->get_padded_n_parameters() +
                      FrameHeaderSize + fixture.code_object->n_locals,
                  locations[6].register_index);
    }

    TEST(JitBytecodeState, ReadsSourcesBeforeApplyingMultipleResults)
    {
        StateFixture fixture;
        BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
        BytecodeState<TestRef> state = make_entry_state(tracker);

        std::array<BytecodeValueLocation, 3> sources = {
            BytecodeValueLocation::accumulator(), parameter(1),
            local(*fixture.code_object, 0)};
        std::vector<TestRef> inputs = tracker.read(
            state, std::span<const BytecodeValueLocation>(sources));
        EXPECT_EQ((std::vector<TestRef>{99, 11, 90}), inputs);

        std::array<BytecodeValueLocation, 3> destinations = {
            parameter(1), local(*fixture.code_object, 0),
            BytecodeValueLocation::accumulator()};
        std::array<TestRef, 3> results = {101, 102, 103};
        tracker.write(state,
                      std::span<const BytecodeValueLocation>(destinations),
                      std::span<const TestRef>(results));

        EXPECT_EQ(101u, tracker.value_at(state, parameter(1)));
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

        std::array<BytecodeValueLocation, 1> source = {parameter(0)};
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
        std::array<TestRef, 9> parameters = {1, 2, 3, 4, 5, 6, 7, 8, 9};

        BytecodeState<TestRef> state = tracker.make_state_from_block_parameters(
            std::span<const TestRef>(parameters));
        std::vector<TestRef> arguments = tracker.block_arguments(state);

        EXPECT_EQ((std::vector<TestRef>(parameters.begin(), parameters.end())),
                  arguments);
        EXPECT_EQ(
            1u, tracker.value_at(state, BytecodeValueLocation::accumulator()));
        EXPECT_EQ(4u, tracker.value_at(state, parameter(2)));
        EXPECT_EQ(6u, tracker.value_at(state, local(*fixture.code_object, 1)));
        EXPECT_EQ(9u,
                  tracker.value_at(state, temporary(*fixture.code_object, 2)));
    }

    TEST(JitBytecodeState, RejectsInvalidLocationsAndArities)
    {
        EXPECT_DEATH(([] {
                         StateFixture fixture;
                         BytecodeStateTracker<TestRef> tracker(
                             *fixture.code_object);
                         std::array<TestRef, 2> parameters = {1, 2};
                         (void)tracker.make_entry_state(
                             std::span<const TestRef>(parameters), 90, 99);
                     }()),
                     "wrong parameter count");

        EXPECT_DEATH(
            ([] {
                StateFixture fixture;
                BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
                BytecodeState<TestRef> state = make_entry_state(tracker);
                (void)tracker.value_at(state, local(*fixture.code_object, 2));
            }()),
            "local location is out of range");

        EXPECT_DEATH(
            ([] {
                StateFixture fixture;
                BytecodeStateTracker<TestRef> tracker(*fixture.code_object);
                BytecodeState<TestRef> state = make_entry_state(tracker);
                std::array<BytecodeValueLocation, 1> destinations = {
                    parameter(0)};
                std::span<const TestRef> no_results;
                tracker.write(
                    state, std::span<const BytecodeValueLocation>(destinations),
                    no_results);
            }()),
            "mismatched destination and result counts");
    }

}  // namespace cl::jit
