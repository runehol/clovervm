#include "gc/collection_sources.h"

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
}  // namespace

TEST(GCCollectionSources, EmptySelectionAndEmptySpaceHaveNoSources)
{
    HeapSpace empty;
    HeapSpace outside;
    HeapObject *object = make_header(outside, HeapGenerationState::Old);
    CollectionSources no_sources(std::span<const CollectionSourceSpace>{});
    EXPECT_FALSE(no_sources.is_collected_source(object));
    std::array selected{
        CollectionSourceSpace{&empty, HeapGenerationState::Young}};
    CollectionSources empty_source(selected);
    EXPECT_FALSE(empty_source.is_collected_source(object));
}

TEST(GCCollectionSources, SelectionDistinguishesSourcesFromSameGenerationCopies)
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
        CollectionSourceSpace{&nursery, HeapGenerationState::Young}};
    std::array full_spaces{
        CollectionSourceSpace{&old, HeapGenerationState::Old},
        CollectionSourceSpace{&nursery, HeapGenerationState::Young}};
    CollectionSources minor(young_spaces);
    CollectionSources full(full_spaces);

    EXPECT_TRUE(minor.is_collected_source(young));
    EXPECT_FALSE(minor.is_collected_source(old_object));
    EXPECT_FALSE(minor.is_collected_source(remembered));
    EXPECT_TRUE(full.is_collected_source(young));
    EXPECT_TRUE(full.is_collected_source(old_object));
    EXPECT_TRUE(full.is_collected_source(remembered));
    EXPECT_FALSE(full.is_collected_source(uncollected));
    EXPECT_FALSE(full.is_collected_source(fixed));

    // Destinations may grow after the source snapshot. Old, movable headers
    // alone cannot distinguish them from the selected old source objects.
    for(size_t idx = 0; idx < 3; ++idx)
    {
        auto *copy = make_header(destination, HeapGenerationState::Old,
                                 DefaultSlabSize + 1);
        EXPECT_FALSE(minor.is_collected_source(copy));
        EXPECT_FALSE(full.is_collected_source(copy));
    }
}

TEST(GCCollectionSources, ForwardingPreservesClassificationWithoutReadingLayout)
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
        std::array spaces{CollectionSourceSpace{
            &source, generation == HeapGenerationState::Young
                         ? HeapGenerationState::Young
                         : HeapGenerationState::Old}};
        CollectionSources selected(spaces);
        HeaderBytes before = std::bit_cast<HeaderBytes>(*original);
        EXPECT_TRUE(selected.is_collected_source(original));
        EXPECT_EQ(before, std::bit_cast<HeaderBytes>(*original));

        original->install_forwarding_target(copy);
        second->install_forwarding_target(fixed_copy);
        HeaderBytes forwarded = std::bit_cast<HeaderBytes>(*original);
        EXPECT_TRUE(selected.is_collected_source(original));
        EXPECT_TRUE(selected.is_collected_source(second));
        EXPECT_FALSE(selected.is_collected_source(copy));
        EXPECT_FALSE(selected.is_collected_source(fixed_copy));
        EXPECT_EQ(copy, original->forwarding_target());
        EXPECT_EQ(fixed_copy, second->forwarding_target());
        EXPECT_EQ(generation, original->generation_state());
        EXPECT_EQ(forwarded, std::bit_cast<HeaderBytes>(*original));
    }
}

TEST(GCCollectionSources, FindsEveryMappingAcrossSlabGrowthAndLargeReservations)
{
    HeapSpace source;
    std::vector<HeapObject *> objects;
    for(size_t idx = 0; idx < 16; ++idx)
    {
        objects.push_back(make_header(source, HeapGenerationState::Young,
                                      DefaultSlabSize + idx * 32));
    }
    std::array spaces{
        CollectionSourceSpace{&source, HeapGenerationState::Young}};
    CollectionSources selected(spaces);
    for(HeapObject *object: objects)
    {
        EXPECT_TRUE(selected.is_collected_source(object));
    }
}

TEST(GCCollectionSourcesDeathTest, RejectsNullSpaceAndInvalidSpaceGeneration)
{
    HeapSpace space;
    std::array null_space{
        CollectionSourceSpace{nullptr, HeapGenerationState::Young}};
    EXPECT_DEATH((void)CollectionSources(null_space), "must not be null");
    std::array invalid{
        CollectionSourceSpace{&space, HeapGenerationState::OldRemembered}};
    EXPECT_DEATH((void)CollectionSources(invalid), "must be Young or Old");
}

TEST(GCCollectionSourcesDeathTest, RejectsOverlappingSelections)
{
    HeapSpace space;
    make_header(space, HeapGenerationState::Young);
    std::array duplicate{
        CollectionSourceSpace{&space, HeapGenerationState::Young},
        CollectionSourceSpace{&space, HeapGenerationState::Young}};
    EXPECT_DEATH((void)CollectionSources(duplicate), "mappings overlap");
}

TEST(GCCollectionSourcesDeathTest,
     NeverSkipsNonMovingObjectInsideSelectedStorage)
{
    HeapSpace space;
    auto *object = make_header(space, HeapGenerationState::Young);
    object->make_non_moving();
    std::array spaces{
        CollectionSourceSpace{&space, HeapGenerationState::Young}};
    CollectionSources selected(spaces);
    EXPECT_DEATH((void)selected.is_collected_source(object),
                 "non-moving object");
}

TEST(GCCollectionSourcesDeathTest,
     RejectsGenerationMismatchInsideSelectedStorage)
{
    HeapSpace space;
    auto *young = make_header(space, HeapGenerationState::Young);
    auto *old = make_header(space, HeapGenerationState::Old);
    auto *remembered = make_header(space, HeapGenerationState::OldRemembered);
    std::array young_spaces{
        CollectionSourceSpace{&space, HeapGenerationState::Young}};
    std::array old_spaces{
        CollectionSourceSpace{&space, HeapGenerationState::Old}};
    CollectionSources young_selection(young_spaces);
    CollectionSources old_selection(old_spaces);
    EXPECT_DEATH((void)old_selection.is_collected_source(young),
                 "generation does not match");
    EXPECT_DEATH((void)young_selection.is_collected_source(old),
                 "generation does not match");
    EXPECT_DEATH((void)young_selection.is_collected_source(remembered),
                 "generation does not match");
}
