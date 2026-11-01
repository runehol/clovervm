#include "gc/strong_remembered_set.h"

#include "memory/heap_space.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
            {size_t{0}, size_t{1}, size_t{509}, size_t{510}, size_t{511},
             size_t{1019}, size_t{1020}, size_t{1021}, size_t{1534}, size_t{1},
             size_t{0}, size_t{510}, size_t{1021}, size_t{1534}})
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

    TEST(GCStrongRememberedSet, HandoffPreservesHeadersAcrossBlockBoundaries)
    {
        enum class CacheState
        {
            Fresh,
            Reused
        };
        using Bytes = std::array<std::byte, sizeof(HeapObject)>;
        for(auto cache: {CacheState::Fresh, CacheState::Reused})
            for(size_t destination_count:
                {size_t{0}, size_t{1}, size_t{509}, size_t{510}, size_t{511},
                 size_t{1021}})
                for(size_t source_count:
                    {size_t{0}, size_t{1}, size_t{509}, size_t{510},
                     size_t{511}, size_t{1021}})
                {
                    SCOPED_TRACE(static_cast<int>(cache));
                    SCOPED_TRACE(destination_count);
                    SCOPED_TRACE(source_count);
                    HeapSpace objects;
                    auto *young =
                        make_header(objects, HeapGenerationState::Young);
                    std::vector<HeapObject *> owners;
                    for(size_t index = 0; index < 2046; ++index)
                    {
                        auto *owner =
                            make_header(objects, HeapGenerationState::Old);
                        owner->refcount() = 17;
                        if(index % 2 == 0)
                        {
                            owner->make_non_moving();
                        }
                        owners.push_back(owner);
                    }
                    StrongRememberedSet destination;
                    StrongRememberedSet source;
                    if(cache == CacheState::Reused)
                    {
                        for(StrongRememberedSet *set: {&destination, &source})
                        {
                            for(HeapObject *owner: owners)
                            {
                                set->record_store(owner, young);
                            }
                            set->clear_after_collection();
                        }
                    }
                    std::unordered_set<HeapObject *> expected;
                    std::vector<Bytes> before;
                    for(size_t index = 0;
                        index < destination_count + source_count; ++index)
                    {
                        HeapObject *owner = owners[index];
                        if(index < destination_count)
                        {
                            destination.record_store(owner, young);
                        }
                        else
                        {
                            source.record_store(owner, young);
                        }
                        expected.insert(owner);
                        before.push_back(std::bit_cast<Bytes>(*owner));
                    }
                    // The const-reference visitor exposes the actual entry
                    // cell, not a copied pointer argument. Existing and
                    // incoming entries must retain these addresses through the
                    // splice.
                    std::unordered_map<HeapObject *, HeapObject *const *> cells;
                    auto capture = [&](HeapObject *const &owner) {
                        EXPECT_TRUE(cells.emplace(owner, &owner).second);
                    };
                    destination.for_each_owner(capture);
                    source.for_each_owner(capture);
                    destination.adopt_from(std::move(source));
                    EXPECT_TRUE(source.empty());
                    size_t source_visits = 0;
                    source.for_each_owner(
                        [&](HeapObject *) { ++source_visits; });
                    EXPECT_EQ(0u, source_visits);
                    // An empty source reset must not clear transferred flags,
                    // including stale pointers in its retained spare blocks.
                    source.clear_after_collection();
                    std::unordered_set<HeapObject *> actual;
                    destination.for_each_owner([&](HeapObject *const &owner) {
                        EXPECT_TRUE(actual.insert(owner).second);
                        auto found = cells.find(owner);
                        EXPECT_NE(cells.end(), found);
                        if(found != cells.end())
                        {
                            EXPECT_EQ(found->second, &owner);
                        }
                    });
                    EXPECT_EQ(expected, actual);
                    for(size_t index = 0; index < before.size(); ++index)
                    {
                        EXPECT_EQ(before[index],
                                  std::bit_cast<Bytes>(*owners[index]));
                    }
                    destination.clear_after_collection();
                    for(HeapObject *owner: owners)
                    {
                        EXPECT_EQ(HeapGenerationState::Old,
                                  owner->generation_state());
                    }
                }
    }

    TEST(GCStrongRememberedSet, SpliceExchangesSpareChainsAndResealsOnRefill)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        auto make_owners = [&](size_t count) {
            std::vector<HeapObject *> owners;
            for(size_t index = 0; index < count; ++index)
            {
                owners.push_back(
                    make_header(objects, HeapGenerationState::Old));
            }
            return owners;
        };
        auto source_owners = make_owners(2040);
        auto destination_owners = make_owners(1021);
        auto appended_owners = make_owners(510);
        auto *source_replacement =
            make_header(objects, HeapGenerationState::Old);
        StrongRememberedSet source;
        StrongRememberedSet destination;
        std::unordered_map<HeapObject *, HeapObject *const *> source_cells;
        std::unordered_map<HeapObject *, HeapObject *const *> destination_cells;
        for(HeapObject *owner: source_owners)
        {
            source.record_store(owner, young);
        }
        source.for_each_owner([&](HeapObject *const &owner) {
            source_cells.emplace(owner, &owner);
        });
        source.clear_after_collection();
        for(HeapObject *owner: destination_owners)
        {
            destination.record_store(owner, young);
        }
        destination.for_each_owner([&](HeapObject *const &owner) {
            destination_cells.emplace(owner, &owner);
        });
        destination.clear_after_collection();

        ASSERT_TRUE(source_cells.contains(source_owners[1530]));
        ASSERT_TRUE(source_cells.contains(source_owners[1021]));
        ASSERT_TRUE(destination_cells.contains(destination_owners[510]));
        auto source_spare_cell = source_cells.find(source_owners[1530])->second;
        auto incoming_room_cell =
            source_cells.find(source_owners[1021])->second;
        auto destination_spare_cell =
            destination_cells.find(destination_owners[510])->second;

        for(size_t index = 0; index < 1021; ++index)
        {
            source.record_store(source_owners[index], young);
        }
        for(size_t index = 0; index < 37; ++index)
        {
            destination.record_store(destination_owners[index], young);
        }
        destination.adopt_from(std::move(source));
        // Source receives the receiver's old spare second block. The receiver
        // retains the incoming fourth block as its spare. Addresses prove
        // actual cache exchange, not copying or accumulation of both caches.
        source.record_store(source_replacement, young);
        source.for_each_owner([&](HeapObject *const &owner) {
            EXPECT_EQ(source_replacement, owner);
            EXPECT_EQ(destination_spare_cell, &owner);
        });
        for(HeapObject *owner: appended_owners)
        {
            destination.record_store(owner, young);
        }
        size_t visits = 0;
        destination.for_each_owner([&](HeapObject *const &owner) {
            ++visits;
            if(owner == appended_owners[0])
            {
                EXPECT_EQ(incoming_room_cell, &owner);
            }
            if(owner == appended_owners[509])
            {
                EXPECT_EQ(source_spare_cell, &owner);
            }
        });
        EXPECT_EQ(37u + 1021u + 510u, visits);
        source.clear_after_collection();
        destination.clear_after_collection();

        // Refill past the formerly 37-entry sealed block. Rollover must replace
        // its stale sealed_count with 510, rather than dropping those entries.
        std::unordered_set<HeapObject *> expected;
        for(size_t index = 0; index < 700; ++index)
        {
            expected.insert(source_owners[index]);
            destination.record_store(source_owners[index], young);
        }
        std::unordered_set<HeapObject *> actual;
        destination.for_each_owner([&](HeapObject *owner) {
            EXPECT_TRUE(actual.insert(owner).second);
        });
        EXPECT_EQ(expected, actual);
        destination.clear_after_collection();
        for(HeapObject *owner: expected)
        {
            EXPECT_EQ(HeapGenerationState::Old, owner->generation_state());
        }
    }

    TEST(GCStrongRememberedSet, FreshSourcesReceiveTheReceiversDisplacedCache)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        auto *owner = make_header(objects, HeapGenerationState::Old);
        auto *replacement = make_header(objects, HeapGenerationState::Old);
        StrongRememberedSet receiver;
        HeapObject *const *previous_cell = nullptr;
        for(size_t cycle = 0; cycle < 100; ++cycle)
        {
            {
                StrongRememberedSet source;
                source.record_store(owner, young);
                HeapObject *const *incoming_cell = nullptr;
                source.for_each_owner([&](HeapObject *const &entry) {
                    EXPECT_EQ(owner, entry);
                    incoming_cell = &entry;
                });
                receiver.adopt_from(std::move(source));
                receiver.for_each_owner([&](HeapObject *const &entry) {
                    EXPECT_EQ(owner, entry);
                    EXPECT_EQ(incoming_cell, &entry);
                });
                if(previous_cell != nullptr)
                {
                    // A repeated one-owner handoff must return the entire old
                    // receiver cache, not accumulate another spare block there.
                    source.record_unremembered_owner(replacement);
                    source.for_each_owner([&](HeapObject *const &entry) {
                        EXPECT_EQ(replacement, entry);
                        EXPECT_EQ(previous_cell, &entry);
                    });
                    source.clear_after_collection();
                }
                previous_cell = incoming_cell;
                EXPECT_EQ(HeapGenerationState::OldRemembered,
                          owner->generation_state());
            }
            // Destroying the source freed the displaced cache, not the active
            // receiver entry. Its owner flag remains for the receiver to reset.
            size_t visits = 0;
            receiver.for_each_owner([&](HeapObject *entry) {
                EXPECT_EQ(owner, entry);
                ++visits;
            });
            EXPECT_EQ(1u, visits);
            receiver.clear_after_collection();
        }
    }

    TEST(GCStrongRememberedSet, ChainedHandoffsSurviveSourceDestruction)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        std::vector<HeapObject *> owners;
        for(size_t index = 0; index < 1023; ++index)
        {
            owners.push_back(make_header(objects, HeapGenerationState::Old));
        }
        StrongRememberedSet receiver;
        receiver.record_store(owners[0], young);
        {
            StrongRememberedSet intermediate;
            intermediate.record_store(owners[1], young);
            {
                StrongRememberedSet source;
                for(size_t index = 2; index < owners.size(); ++index)
                {
                    source.record_store(owners[index], young);
                }
                intermediate.adopt_from(std::move(source));
                EXPECT_TRUE(source.empty());
            }
            receiver.adopt_from(std::move(intermediate));
            EXPECT_TRUE(intermediate.empty());
        }
        std::unordered_set<HeapObject *> actual;
        receiver.for_each_owner([&](HeapObject *owner) {
            EXPECT_TRUE(actual.insert(owner).second);
        });
        EXPECT_EQ(owners.size(), actual.size());
        for(HeapObject *owner: owners)
        {
            EXPECT_TRUE(actual.contains(owner));
            EXPECT_EQ(HeapGenerationState::OldRemembered,
                      owner->generation_state());
        }
        receiver.clear_after_collection();
    }

    TEST(GCStrongRememberedSet,
         SourceCanBeReusedWithoutLosingTransferredMembership)
    {
        HeapSpace objects;
        auto *young = make_header(objects, HeapGenerationState::Young);
        auto *first = make_header(objects, HeapGenerationState::Old);
        auto *second = make_header(objects, HeapGenerationState::Old);
        StrongRememberedSet destination;
        StrongRememberedSet source;
        source.record_store(first, young);
        destination.adopt_from(std::move(source));
        // The source still observes global header membership in destination.
        source.record_store(first, young);
        EXPECT_TRUE(source.empty());
        source.record_store(second, young);
        destination.adopt_from(std::move(source));
        destination.adopt_from(
            std::move(source));  // Empty handoff is harmless.
        size_t visits = 0;
        destination.for_each_owner([&](HeapObject *) { ++visits; });
        EXPECT_EQ(2u, visits);
        destination.clear_after_collection();
        // After the actual collection reset, either buffer may record again.
        source.record_store(first, young);
        EXPECT_FALSE(source.empty());
        EXPECT_TRUE(destination.empty());
        source.clear_after_collection();
    }

#ifndef NDEBUG
    TEST(GCStrongRememberedSetDeathTest, RejectsSelfAdoption)
    {
        StrongRememberedSet set;
        EXPECT_DEATH(set.adopt_from(std::move(set)), "this != &source");
    }
#endif

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
