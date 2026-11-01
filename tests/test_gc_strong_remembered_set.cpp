#include "gc/strong_remembered_set.h"

#include "memory/heap_space.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace
{
    using namespace cl;
    using namespace cl::gc;

    HeapObject *make_header(HeapSpace &space, HeapGenerationState generation)
    {
        auto *object = std::construct_at(
            reinterpret_cast<HeapObject *>(space.allocate(sizeof(HeapObject))),
            NativeLayoutId::TestOnly, uint16_t{0xbeef});
        object->set_generation_state(generation);
        return object;
    }

    TEST(GCStrongRememberedSet, EmptyResetAndOwnership)
    {
        static_assert(!std::is_copy_constructible_v<StrongRememberedSet>);
        static_assert(!std::is_move_constructible_v<StrongRememberedSet>);
        StrongRememberedSet set;
        EXPECT_TRUE(set.empty());
        size_t visits = 0;
        set.for_each_owner([&](HeapObject *) { ++visits; });
        set.clear_after_collection();
        set.clear_after_collection();
        set.for_each_owner([&](HeapObject *) { ++visits; });
        EXPECT_EQ(0u, visits);
    }

    TEST(GCStrongRememberedSet, FiltersStoresAndDeduplicatesWithHeaderState)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        auto *old_child = make_header(objects, HeapGenerationState::Old);
        auto *owner = make_header(objects, HeapGenerationState::Old);
        StrongRememberedSet set;
        set.record_store(young, young);
        set.record_store(young, old_child);
        set.record_store(owner, nullptr);
        set.record_store(owner, old_child);
        EXPECT_TRUE(set.empty());
        set.record_store(owner, young);
        EXPECT_FALSE(set.empty());
        EXPECT_EQ(HeapGenerationState::OldRemembered,
                  owner->generation_state());
        for(size_t index = 0; index < 100; ++index)
        {
            set.record_store(owner, young);
        }
        set.record_store(owner, nullptr);
        set.record_store(owner, old_child);
        size_t visits = 0;
        set.for_each_owner([&](HeapObject *entry) {
            EXPECT_EQ(owner, entry);
            ++visits;
        });
        EXPECT_EQ(1u, visits);
        set.clear_after_collection();
        EXPECT_EQ(HeapGenerationState::Old, owner->generation_state());
        EXPECT_EQ(HeapGenerationState::Young, young->generation_state());
    }

    TEST(GCStrongRememberedSet, PreconditionedRecordingCrossesAndReusesBlocks)
    {
        HeapSpace objects;
        std::vector<HeapObject *> owners;
        for(size_t index = 0; index < 1023; ++index)
        {
            auto *owner = make_header(objects, HeapGenerationState::Old);
            if(index % 2 == 0)
            {
                owner->make_non_moving();
            }
            owners.push_back(owner);
        }
        StrongRememberedSet set;
        for(size_t count: {size_t{1023}, size_t{1}, size_t{512}, size_t{1023}})
        {
            for(size_t index = 0; index < count; ++index)
            {
                // Conservative recording needs no child, but the caller must
                // prove Old membership state before using this primitive.
                set.record_unremembered_owner(owners[index]);
            }
            std::unordered_set<HeapObject *> actual;
            set.for_each_owner([&](HeapObject *owner) {
                EXPECT_TRUE(actual.insert(owner).second);
            });
            EXPECT_EQ(count, actual.size());
            for(size_t index = 0; index < count; ++index)
            {
                EXPECT_TRUE(actual.contains(owners[index]));
                EXPECT_EQ(HeapGenerationState::OldRemembered,
                          owners[index]->generation_state());
                EXPECT_EQ(index % 2 == 0, owners[index]->is_non_moving());
            }
            set.clear_after_collection();
        }
    }

    TEST(GCStrongRememberedSet, PreservesOtherHeaderStateForBothMobilities)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        auto *movable = make_header(objects, HeapGenerationState::Old);
        auto *fixed = make_header(objects, HeapGenerationState::Old);
        fixed->make_non_moving();
        using Bytes = std::array<std::byte, sizeof(HeapObject)>;
        for(HeapObject *owner: {movable, fixed})
        {
            owner->refcount() = 17;
            owner->set_lifecycle_state(HeapLifecycleState::InZct);
            Bytes before = std::bit_cast<Bytes>(*owner);
            StrongRememberedSet set;
            set.record_store(owner, young);
            EXPECT_EQ(17, owner->refcount());
            EXPECT_EQ(uint16_t{0xbeef}, owner->native_layout_aux_count_value());
            EXPECT_EQ(NativeLayoutId::TestOnly, owner->native_layout_id());
            EXPECT_EQ(HeapLifecycleState::InZct, owner->lifecycle_state());
            EXPECT_EQ(owner == fixed, owner->is_non_moving());
            set.clear_after_collection();
            EXPECT_EQ(before, std::bit_cast<Bytes>(*owner));
        }
    }

    TEST(GCStrongRememberedSet, IteratesOnlyActivePrefixesAcrossBlockReuse)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        std::vector<HeapObject *> owners;
        for(size_t index = 0; index < 1534; ++index)
        {
            owners.push_back(make_header(objects, HeapGenerationState::Old));
        }
        StrongRememberedSet set;
        // Exact block ends, rollover, then smaller and larger reused prefixes.
        // Inactive cells retain stale values and must not be visited/reset.
        for(size_t count:
            {size_t{0}, size_t{1}, size_t{510}, size_t{511}, size_t{512},
             size_t{1022}, size_t{1023}, size_t{1534}, size_t{1}, size_t{0},
             size_t{511}, size_t{1023}, size_t{1534}})
        {
            SCOPED_TRACE(count);
            std::unordered_set<HeapObject *> expected;
            for(size_t index = 0; index < count; ++index)
            {
                HeapObject *owner = owners[owners.size() - index - 1];
                expected.insert(owner);
                set.record_store(owner, young);
                set.record_store(owner, young);
            }
            std::unordered_set<HeapObject *> actual;
            set.for_each_owner([&](HeapObject *owner) {
                EXPECT_TRUE(actual.insert(owner).second);
                EXPECT_EQ(HeapGenerationState::OldRemembered,
                          owner->generation_state());
            });
            EXPECT_EQ(expected, actual);
            EXPECT_EQ(count == 0, set.empty());
            set.clear_after_collection();
            size_t visits = 0;
            set.for_each_owner([&](HeapObject *) { ++visits; });
            EXPECT_EQ(0u, visits);
            for(HeapObject *owner: owners)
            {
                EXPECT_EQ(HeapGenerationState::Old, owner->generation_state());
            }
        }
    }

    TEST(GCStrongRememberedSet, CachedBlocksDoNotKeepOldOwnerStorageAccessible)
    {
        StrongRememberedSet set;
        {
            HeapSpace objects;
            auto *young = make_header(objects, HeapGenerationState::Young);
            for(size_t index = 0; index < 1023; ++index)
            {
                set.record_store(make_header(objects, HeapGenerationState::Old),
                                 young);
            }
            set.clear_after_collection();
        }
        size_t visits = 0;
        set.for_each_owner([&](HeapObject *) { ++visits; });
        EXPECT_EQ(0u, visits);
        set.clear_after_collection();
        {
            HeapSpace replacement;
            auto *young = make_header(replacement, HeapGenerationState::Young);
            auto *owner = make_header(replacement, HeapGenerationState::Old);
            set.record_store(owner, young);
            set.for_each_owner([&](HeapObject *entry) {
                EXPECT_EQ(owner, entry);
                ++visits;
            });
            EXPECT_EQ(1u, visits);
            set.clear_after_collection();
        }
        // Destruction must also ignore inactive cells whose owners are gone.
    }

    TEST(GCStrongRememberedSet, HeaderMembershipCanBelongToAnotherThreadSet)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        auto *owner = make_header(objects, HeapGenerationState::Old);
        StrongRememberedSet first;
        StrongRememberedSet second;
        first.record_store(owner, young);
        second.record_store(owner, young);
        EXPECT_FALSE(first.empty());
        EXPECT_TRUE(second.empty());
        // Serialized stand-in for per-thread buffers. All buffers participate
        // in the same collection; this does not test concurrent publication.
        first.clear_after_collection();
        second.clear_after_collection();
        second.record_store(owner, young);
        EXPECT_TRUE(first.empty());
        EXPECT_FALSE(second.empty());
        second.clear_after_collection();
    }
}  // namespace
