#include "gc/evacuation_spaces.h"

#include "memory/heap_constants.h"
#include "memory/heap_space.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

using namespace cl;
using namespace cl::gc;

namespace
{
    // Disposable headers in raw storage, never registered with the RC heap.
    // Oversized reservations below exercise mappings, not layout admission.
    HeapObject *make_header(HeapSpace &space, HeapGenerationState generation,
                            size_t extent = sizeof(HeapObject))
    {
        auto *object = std::construct_at(
            reinterpret_cast<HeapObject *>(space.allocate(extent)),
            NativeLayoutId::TestOnly, uint16_t{0xbeef});
        object->set_generation_state(generation);
        return object;
    }

    using HeaderBytes = std::array<std::byte, sizeof(HeapObject)>;

    void expect_disjoint_mappings(const HeapSpace &left, const HeapSpace &right)
    {
        left.visit_memory_ranges([&](uintptr_t left_begin, uintptr_t left_end) {
            right.visit_memory_ranges([&](uintptr_t right_begin,
                                          uintptr_t right_end) {
                EXPECT_TRUE(left_end <= right_begin || right_end <= left_begin);
            });
        });
    }
}  // namespace

TEST(GCEvacuationSpaces, MinorRoutingAppendsToExistingDestinations)
{
    HeapSpace nursery;
    HeapSpace old;
    HeapSpace non_moving;
    auto *existing_old = make_header(old, HeapGenerationState::Old);
    auto *existing_fixed = make_header(non_moving, HeapGenerationState::Old);
    existing_fixed->make_non_moving();
    HeaderBytes old_before = std::bit_cast<HeaderBytes>(*existing_old);
    HeaderBytes fixed_before = std::bit_cast<HeaderBytes>(*existing_fixed);
    std::array<HeapObject *, 4> inputs;
    for(auto &input: inputs)
    {
        input = make_header(nursery, HeapGenerationState::Young);
    }
    std::array spaces{EvacuationSpace{&nursery, HeapGenerationState::Young}};
    EvacuationSpaces selected(spaces);

    // Raw headers exercise routing and storage only, not copying or GC lifetime
    // authority. Alternate destinations and force both to grow new mappings.
    for(size_t index = 0; index < inputs.size(); ++index)
    {
        HeapSpace &destination = index % 2 == 0 ? old : non_moving;
        auto *copy = make_header(destination, HeapGenerationState::Old,
                                 DefaultSlabSize + 1);
        if(index % 2 != 0)
        {
            copy->make_non_moving();
        }
        inputs[index]->install_forwarding_target(copy);
        EXPECT_TRUE(selected.contains(inputs[index]));
        EXPECT_FALSE(selected.contains(copy));
        EXPECT_EQ(copy, inputs[index]->forwarding_target());
    }
    EXPECT_FALSE(selected.contains(existing_old));
    EXPECT_FALSE(selected.contains(existing_fixed));
    EXPECT_EQ(old_before, std::bit_cast<HeaderBytes>(*existing_old));
    EXPECT_EQ(fixed_before, std::bit_cast<HeaderBytes>(*existing_fixed));
    expect_disjoint_mappings(nursery, old);
    expect_disjoint_mappings(nursery, non_moving);
    expect_disjoint_mappings(old, non_moving);
}

TEST(GCEvacuationSpaces, FullRoutingRetainsFreshOldAndExistingNonMovingStorage)
{
    HeapSpace next_old;
    HeapSpace non_moving;
    auto *existing_fixed = make_header(non_moving, HeapGenerationState::Old);
    existing_fixed->make_non_moving();
    HeaderBytes fixed_before = std::bit_cast<HeaderBytes>(*existing_fixed);
    std::vector<HeapObject *> survivors;
    {
        HeapSpace nursery;
        HeapSpace previous_old;
        std::array inputs{
            make_header(nursery, HeapGenerationState::Young),
            make_header(previous_old, HeapGenerationState::Old),
            make_header(previous_old, HeapGenerationState::OldRemembered)};
        std::array spaces{
            EvacuationSpace{&nursery, HeapGenerationState::Young},
            EvacuationSpace{&previous_old, HeapGenerationState::Old}};
        EvacuationSpaces selected(spaces);
        for(HeapObject *input: inputs)
        {
            auto *ordinary = make_header(next_old, HeapGenerationState::Old,
                                         DefaultSlabSize + 1);
            auto *fixed = make_header(non_moving, HeapGenerationState::Old,
                                      DefaultSlabSize + 1);
            fixed->make_non_moving();
            survivors.push_back(ordinary);
            survivors.push_back(fixed);
            input->install_forwarding_target(ordinary);
            EXPECT_TRUE(selected.contains(input));
            EXPECT_FALSE(selected.contains(ordinary));
            EXPECT_FALSE(selected.contains(fixed));
        }
        for(const HeapSpace *source: {&nursery, &previous_old})
        {
            expect_disjoint_mappings(*source, next_old);
            expect_disjoint_mappings(*source, non_moving);
        }
        expect_disjoint_mappings(next_old, non_moving);
        // The containment index dies before its borrowed source mappings.
        // These disposable headers have no RC children or runtime registration;
        // this is not permission to discard production RC-owned storage.
    }
    for(HeapObject *survivor: survivors)
    {
        EXPECT_EQ(HeapGenerationState::Old, survivor->generation_state());
        EXPECT_EQ(uint16_t{0xbeef}, survivor->native_layout_aux_count_value());
    }
    EXPECT_EQ(fixed_before, std::bit_cast<HeaderBytes>(*existing_fixed));
    // Subsequent minor promotions append into the retained full destination.
    make_header(next_old, HeapGenerationState::Old);
    EXPECT_EQ(HeapGenerationState::Old, survivors.front()->generation_state());
}

TEST(GCEvacuationSpaces, EmptySelectionAndEmptySpaceContainNoObjects)
{
    HeapSpace empty;
    HeapSpace outside;
    HeapObject *object = make_header(outside, HeapGenerationState::Old);
    EvacuationSpaces no_sources(std::span<const EvacuationSpace>{});
    EXPECT_FALSE(no_sources.contains(object));
    std::array selected{EvacuationSpace{&empty, HeapGenerationState::Young}};
    EvacuationSpaces empty_source(selected);
    EXPECT_FALSE(empty_source.contains(object));
}

TEST(GCEvacuationSpaces, ContainsSelectedInputsButNotSameGenerationCopies)
{
    HeapSpace nursery;
    HeapSpace old;
    HeapSpace destination;
    HeapSpace stable;
    HeapSpace unselected;
    auto *young = make_header(nursery, HeapGenerationState::Young);
    auto *old_object = make_header(old, HeapGenerationState::Old);
    auto *remembered = make_header(old, HeapGenerationState::OldRemembered);
    auto *uncollected = make_header(unselected, HeapGenerationState::Old);
    auto *fixed = make_header(stable, HeapGenerationState::Old);
    fixed->make_non_moving();
    std::array young_spaces{
        EvacuationSpace{&nursery, HeapGenerationState::Young}};
    std::array full_spaces{
        EvacuationSpace{&old, HeapGenerationState::Old},
        EvacuationSpace{&nursery, HeapGenerationState::Young}};
    EvacuationSpaces minor(young_spaces);
    EvacuationSpaces full(full_spaces);

    EXPECT_TRUE(minor.contains(young));
    EXPECT_FALSE(minor.contains(old_object));
    EXPECT_FALSE(minor.contains(remembered));
    EXPECT_TRUE(full.contains(young));
    EXPECT_TRUE(full.contains(old_object));
    EXPECT_TRUE(full.contains(remembered));
    EXPECT_FALSE(full.contains(uncollected));
    EXPECT_FALSE(full.contains(fixed));

    // Destinations may grow after the source snapshot. Old, movable headers
    // alone cannot distinguish them from the selected old source objects.
    for(size_t idx = 0; idx < 3; ++idx)
    {
        auto *copy = make_header(destination, HeapGenerationState::Old,
                                 DefaultSlabSize + 1);
        EXPECT_FALSE(minor.contains(copy));
        EXPECT_FALSE(full.contains(copy));
    }
}

TEST(GCEvacuationSpaces, ForwardingPreservesContainmentWithoutReadingLayout)
{
    for(HeapGenerationState generation:
        {HeapGenerationState::Young, HeapGenerationState::Old,
         HeapGenerationState::OldRemembered})
    {
        HeapSpace source;
        HeapSpace destination;
        auto *original = make_header(source, generation);
        auto *copy = make_header(destination, HeapGenerationState::Old);
        auto *fixed_copy = make_header(destination, HeapGenerationState::Old);
        fixed_copy->make_non_moving();
        auto *second = make_header(source, generation);
        std::array spaces{
            EvacuationSpace{&source, generation == HeapGenerationState::Young
                                         ? HeapGenerationState::Young
                                         : HeapGenerationState::Old}};
        EvacuationSpaces selected(spaces);
        HeaderBytes before = std::bit_cast<HeaderBytes>(*original);
        EXPECT_TRUE(selected.contains(original));
        EXPECT_EQ(before, std::bit_cast<HeaderBytes>(*original));

        original->install_forwarding_target(copy);
        second->install_forwarding_target(fixed_copy);
        HeaderBytes forwarded = std::bit_cast<HeaderBytes>(*original);
        EXPECT_TRUE(selected.contains(original));
        EXPECT_TRUE(selected.contains(second));
        EXPECT_FALSE(selected.contains(copy));
        EXPECT_FALSE(selected.contains(fixed_copy));
        EXPECT_EQ(copy, original->forwarding_target());
        EXPECT_EQ(fixed_copy, second->forwarding_target());
        EXPECT_EQ(generation, original->generation_state());
        EXPECT_EQ(forwarded, std::bit_cast<HeaderBytes>(*original));
    }
}

TEST(GCEvacuationSpaces, FindsEveryMappingAcrossSlabGrowthAndLargeReservations)
{
    HeapSpace source;
    std::vector<HeapObject *> objects;
    for(size_t idx = 0; idx < 16; ++idx)
    {
        objects.push_back(make_header(source, HeapGenerationState::Young,
                                      DefaultSlabSize + idx * 32));
    }
    std::array spaces{EvacuationSpace{&source, HeapGenerationState::Young}};
    EvacuationSpaces selected(spaces);
    for(HeapObject *object: objects)
    {
        EXPECT_TRUE(selected.contains(object));
    }
}

TEST(GCEvacuationSpacesDeathTest, RejectsNullSpaceAndInvalidSpaceGeneration)
{
    HeapSpace space;
    std::array null_space{EvacuationSpace{nullptr, HeapGenerationState::Young}};
    EXPECT_DEATH((void)EvacuationSpaces(null_space), "must not be null");
    std::array invalid{
        EvacuationSpace{&space, HeapGenerationState::OldRemembered}};
    EXPECT_DEATH((void)EvacuationSpaces(invalid), "must be Young or Old");
}

TEST(GCEvacuationSpacesDeathTest, RejectsOverlappingSelections)
{
    HeapSpace space;
    make_header(space, HeapGenerationState::Young);
    std::array duplicate{EvacuationSpace{&space, HeapGenerationState::Young},
                         EvacuationSpace{&space, HeapGenerationState::Young}};
    EXPECT_DEATH((void)EvacuationSpaces(duplicate), "mappings overlap");
}

TEST(GCEvacuationSpacesDeathTest,
     NeverSkipsNonMovingObjectInsideSelectedStorage)
{
    HeapSpace space;
    auto *object = make_header(space, HeapGenerationState::Young);
    object->make_non_moving();
    std::array spaces{EvacuationSpace{&space, HeapGenerationState::Young}};
    EvacuationSpaces selected(spaces);
    EXPECT_DEATH((void)selected.contains(object), "non-moving object");
}

TEST(GCEvacuationSpacesDeathTest,
     RejectsGenerationMismatchInsideSelectedStorage)
{
    HeapSpace space;
    auto *young = make_header(space, HeapGenerationState::Young);
    auto *old = make_header(space, HeapGenerationState::Old);
    auto *remembered = make_header(space, HeapGenerationState::OldRemembered);
    std::array young_spaces{
        EvacuationSpace{&space, HeapGenerationState::Young}};
    std::array old_spaces{EvacuationSpace{&space, HeapGenerationState::Old}};
    EvacuationSpaces young_selection(young_spaces);
    EvacuationSpaces old_selection(old_spaces);
    EXPECT_DEATH((void)old_selection.contains(young),
                 "generation does not match");
    EXPECT_DEATH((void)young_selection.contains(old),
                 "generation does not match");
    EXPECT_DEATH((void)young_selection.contains(remembered),
                 "generation does not match");
}
