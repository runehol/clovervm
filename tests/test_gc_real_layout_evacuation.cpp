#include "gc/clover_heap_adapter.h"
#include "gc/evacuation.h"
#include "gc/evacuation_destinations.h"
#include "gc/evacuation_spaces.h"
#include "memory/heap_constants.h"
#include "memory/heap_space.h"
#include "object_model/overflow_slots.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{
    using namespace cl;
    using namespace cl::gc;

    enum class CollectionKind
    {
        Young,
        Full
    };
    enum class Destination
    {
        Old,
        NonMoving
    };

    // These objects never enter the RC heap or ZCT. Initialize fixture edges
    // without retaining stores, and discard raw slabs without releasing cells
    // or invoking destructors (including after destructive forwarding).
    OverflowSlots *make_slots(HeapSpace &space, uint32_t capacity,
                              HeapGenerationState generation)
    {
        auto *object = std::construct_at(
            reinterpret_cast<OverflowSlots *>(
                space.allocate(OverflowSlots::allocation_size_for(capacity))),
            0u, capacity);
        object->set_generation_state(generation);
        return object;
    }

    void connect(OverflowSlots *owner, size_t index, HeapObject *target)
    {
        static_assert(sizeof(Value) == sizeof(target));
        ASSERT_LT(index, owner->get_capacity());
        std::memcpy(owner->slot_value_base() + index, &target, sizeof(target));
    }

    HeapObject *edge(OverflowSlots *owner, size_t index)
    {
        return HeapReferenceSlot(owner->slot_value_base() + index)
            .load_pointer();
    }

    std::vector<std::byte> snapshot_payload(OverflowSlots *object)
    {
        size_t size =
            OverflowSlots::copy_size_in_bytes(object) - sizeof(HeapObject);
        std::vector<std::byte> bytes(size);
        std::memcpy(bytes.data(),
                    reinterpret_cast<std::byte *>(object) + sizeof(HeapObject),
                    size);
        return bytes;
    }

    void expect_unchanged_payload(HeapObject *source,
                                  std::span<const std::byte> before)
    {
        // Never ask a forwarded source for layout/aux/RC metadata.
        EXPECT_EQ(0, std::memcmp(before.data(),
                                 reinterpret_cast<std::byte *>(source) +
                                     sizeof(HeapObject),
                                 before.size()));
    }

    struct WeakRoot
    {
        HeapObject *target;
        size_t repairs = 0;

        static WeakRepairResult repair(WeakRoot &root,
                                       cl::WeakResolver resolver)
        {
            ++root.repairs;
            if(root.target == nullptr)
            {
                return WeakRepairResult::Complete;
            }
            auto result = resolver(root.target);
            switch(result.state)
            {
                case WeakState::Resolved:
                    root.target = result.target;
                    return WeakRepairResult::Complete;
                case WeakState::Pending:
                    return WeakRepairResult::Pending;
                case WeakState::Dead:
                    root.target = nullptr;
                    return WeakRepairResult::Complete;
            }
            cl::fatal("invalid weak resolution in test");
        }

        cl::WeakRepairUnit unit()
        {
            return cl::WeakRepairUnit::bind<repair>(*this);
        }
    };

    template <CollectionKind Kind, typename Placement>
    class RealLayoutTestAdapter
    {
    public:
        using Object = HeapObject;
        using Slot = HeapReferenceSlot;
        using WeakUnit = cl::WeakRepairUnit;

        RealLayoutTestAdapter(const EvacuationSpaces &inputs,
                              EvacuationDestinations &destinations,
                              Placement placement)
            : inputs_(inputs), destinations_(destinations),
              placement_(std::move(placement))
        {
        }

        bool is_collected(Object *object) const
        {
            if constexpr(Kind == CollectionKind::Full)
            {
                if(object->is_non_moving())
                {
                    return true;
                }
            }
            return inputs_.contains(object);
        }

        bool is_movable(Object *object) const
        {
            return !object->is_non_moving();
        }

        Object *forwarded_to(Object *object) const
        {
            return object->is_forwarded() ? object->forwarding_target()
                                          : nullptr;
        }

        Object *copy_object(Object *source)
        {
            Object *copy;
            switch(placement_(source))
            {
                case Destination::Old:
                    copy = destinations_.copy_to_old(source);
                    break;
                case Destination::NonMoving:
                    copy = destinations_.copy_to_non_moving(source);
                    break;
                default:
                    fatal("invalid placement in real-layout test");
            }
            copies_.push_back(copy);
            return copy;
        }

        void install_forwarding(Object *source, Object *destination)
        {
            source->install_forwarding_target(destination);
        }

        bool try_mark(Object *object) { return tracing_.try_mark(object); }
        bool is_marked(Object *object) const
        {
            return tracing_.is_marked(object);
        }

        template <typename Visitor>
        void visit_strong_slots(Object *object, Visitor &&visitor)
        {
            ASSERT_FALSE(object->is_forwarded());
            ++scans_[object];
            tracing_.visit_strong_slots(object, std::forward<Visitor>(visitor));
        }

        template <typename Visitor>
        void visit_weak_units(Object *object, Visitor &&visitor)
        {
            ASSERT_FALSE(object->is_forwarded());
            tracing_.visit_weak_units(object, std::forward<Visitor>(visitor));
        }

        [[noreturn]] void allocation_failure() const
        {
            fatal("real-layout evacuation test allocation failed");
        }

        size_t scan_count(Object *object) const
        {
            auto found = scans_.find(object);
            return found == scans_.end() ? 0 : found->second;
        }

        std::span<Object *const> copies() const { return copies_; }

        void expect_canonical_copies() const
        {
            for(Object *copy: copies_)
            {
                EXPECT_FALSE(copy->is_forwarded());
                EXPECT_EQ(HeapGenerationState::Old, copy->generation_state());
                EXPECT_EQ(0, copy->refcount());
                EXPECT_EQ(1u, scan_count(copy));
                EXPECT_TRUE(is_marked(copy));
                EXPECT_FALSE(inputs_.contains(copy));
                visit_strong_reference_slots(copy, [&](Slot slot) {
                    if(Object *target = slot.load_pointer())
                    {
                        EXPECT_FALSE(inputs_.contains(target));
                    }
                });
            }
        }

    private:
        CloverHeapAdapter tracing_;
        const EvacuationSpaces &inputs_;
        EvacuationDestinations &destinations_;
        Placement placement_;
        absl::flat_hash_map<Object *, size_t> scans_;
        std::vector<Object *> copies_;
    };

    TEST(GCRealLayoutEvacuation, MinorUsesRememberedSlotsAndRetainsDestinations)
    {
        HeapSpace old;
        HeapSpace non_moving;
        auto *old_owner = make_slots(old, 1, HeapGenerationState::Old);
        auto *fixed_owner = make_slots(non_moving, 1, HeapGenerationState::Old);
        fixed_owner->make_non_moving();
        HeapObject *root = old_owner;
        HeapObject *fixed_root = fixed_owner;
        WeakRoot dead{nullptr};
        {
            HeapSpace nursery;
            auto *first = make_slots(nursery, 3, HeapGenerationState::Young);
            auto *second = make_slots(nursery, 1, HeapGenerationState::Young);
            dead.target = make_slots(nursery, 0, HeapGenerationState::Young);
            connect(first, 0, second);
            connect(first, 1, second);
            first->slot_value_base()[2] = Value::from_smi(73);
            connect(second, 0, first);
            connect(old_owner, 0, first);
            connect(fixed_owner, 0, second);
            auto first_before = snapshot_payload(first);
            auto second_before = snapshot_payload(second);
            std::array spaces{
                EvacuationSpace{&nursery, HeapGenerationState::Young}};
            EvacuationSpaces inputs(spaces);
            EvacuationDestinations destinations(old, non_moving);
            auto placement = [second](HeapObject *object) {
                return object == second ? Destination::NonMoving
                                        : Destination::Old;
            };
            RealLayoutTestAdapter<CollectionKind::Young, decltype(placement)>
                adapter(inputs, destinations, placement);
            Evacuation collection(adapter);
            collection.add_weak_root(dead.unit());
            collection.add_root(HeapReferenceSlot(&root));
            collection.add_root(HeapReferenceSlot(&fixed_root));
            collection.drain();
            EXPECT_TRUE(adapter.copies().empty());
            collection.add_root(
                HeapReferenceSlot(old_owner->slot_value_base()));
            collection.add_root(
                HeapReferenceSlot(fixed_owner->slot_value_base()));
            collection.finish();
            ASSERT_EQ(2u, adapter.copies().size());
            adapter.expect_canonical_copies();
            EXPECT_EQ(0u, adapter.scan_count(old_owner));
            EXPECT_EQ(0u, adapter.scan_count(fixed_owner));
            ASSERT_TRUE(first->is_forwarded());
            ASSERT_TRUE(second->is_forwarded());
            EXPECT_EQ(first->forwarding_target(), edge(old_owner, 0));
            EXPECT_EQ(second->forwarding_target(), edge(fixed_owner, 0));
            EXPECT_TRUE(inputs.contains(first));
            EXPECT_TRUE(inputs.contains(second));
            expect_unchanged_payload(first, first_before);
            expect_unchanged_payload(second, second_before);
            EXPECT_EQ(nullptr, dead.target);
            EXPECT_EQ(2u, dead.repairs);
        }
        EXPECT_EQ(old_owner, root);
        EXPECT_EQ(fixed_owner, fixed_root);
        auto *first = static_cast<OverflowSlots *>(edge(old_owner, 0));
        auto *second = static_cast<OverflowSlots *>(edge(fixed_owner, 0));
        EXPECT_FALSE(first->is_non_moving());
        EXPECT_TRUE(second->is_non_moving());
        EXPECT_EQ(second, edge(first, 0));
        EXPECT_EQ(second, edge(first, 1));
        EXPECT_EQ(first, edge(second, 0));
        EXPECT_EQ(Value::from_smi(73), first->get(2));
    }

    TEST(GCRealLayoutEvacuation, FullRepairsMixedCyclesAndRealWeakRoots)
    {
        HeapSpace next_old;
        HeapSpace non_moving;
        auto *owner = make_slots(non_moving, 1, HeapGenerationState::Old);
        auto *dead_fixed = make_slots(non_moving, 0, HeapGenerationState::Old);
        owner->make_non_moving();
        dead_fixed->make_non_moving();
        HeapObject *root = owner;
        WeakRoot live{nullptr};
        WeakRoot dead{nullptr};
        WeakRoot fixed{dead_fixed};
        WeakRoot fresh{nullptr};
        {
            HeapSpace nursery;
            HeapSpace previous_old;
            auto *first = make_slots(nursery, 2, HeapGenerationState::Young);
            auto *second =
                make_slots(previous_old, 1, HeapGenerationState::Old);
            auto *third =
                make_slots(previous_old, 1, HeapGenerationState::OldRemembered);
            dead.target = make_slots(previous_old, 0, HeapGenerationState::Old);
            live.target = second;
            connect(owner, 0, first);
            connect(first, 0, second);
            connect(first, 1, third);
            connect(second, 0, first);
            connect(third, 0, second);
            auto first_before = snapshot_payload(first);
            auto second_before = snapshot_payload(second);
            auto third_before = snapshot_payload(third);
            std::array spaces{
                EvacuationSpace{&nursery, HeapGenerationState::Young},
                EvacuationSpace{&previous_old, HeapGenerationState::Old}};
            EvacuationSpaces inputs(spaces);
            EvacuationDestinations destinations(next_old, non_moving);
            auto placement = [second](HeapObject *object) {
                return object == second ? Destination::NonMoving
                                        : Destination::Old;
            };
            RealLayoutTestAdapter<CollectionKind::Full, decltype(placement)>
                adapter(inputs, destinations, placement);
            Evacuation collection(adapter);
            for(WeakRoot *weak: {&live, &dead, &fixed})
            {
                collection.add_weak_root(weak->unit());
            }
            collection.add_root(HeapReferenceSlot(&root));
            collection.add_root(HeapReferenceSlot(&root));
            collection.drain();
            ASSERT_TRUE(first->is_forwarded());
            ASSERT_TRUE(second->is_forwarded());
            ASSERT_TRUE(third->is_forwarded());
            fresh.target = second->forwarding_target();
            collection.add_weak_root(fresh.unit());
            HeapObject *duplicate = first;
            collection.add_root(HeapReferenceSlot(&duplicate));
            collection.finish();
            ASSERT_EQ(3u, adapter.copies().size());
            adapter.expect_canonical_copies();
            EXPECT_EQ(1u, adapter.scan_count(owner));
            EXPECT_EQ(0u, adapter.scan_count(dead_fixed));
            EXPECT_FALSE(adapter.is_marked(dead_fixed));
            EXPECT_EQ(1u, fresh.repairs);
            EXPECT_EQ(fresh.target, live.target);
            EXPECT_EQ(nullptr, dead.target);
            EXPECT_EQ(nullptr, fixed.target);
            EXPECT_EQ(first->forwarding_target(), duplicate);
            ASSERT_EQ(first->forwarding_target(), edge(owner, 0));
            expect_unchanged_payload(first, first_before);
            expect_unchanged_payload(second, second_before);
            expect_unchanged_payload(third, third_before);
        }
        EXPECT_EQ(owner, root);
        auto *first = static_cast<OverflowSlots *>(edge(owner, 0));
        auto *second = static_cast<OverflowSlots *>(edge(first, 0));
        auto *third = static_cast<OverflowSlots *>(edge(first, 1));
        EXPECT_EQ(second, live.target);
        EXPECT_EQ(first, edge(second, 0));
        EXPECT_EQ(second, edge(third, 0));
        EXPECT_TRUE(second->is_non_moving());
        EXPECT_FALSE(first->is_non_moving());
        EXPECT_FALSE(third->is_non_moving());
    }

    TEST(GCRealLayoutEvacuation,
         CapacityTailEdgesSurviveGrowthAndSourceDisposal)
    {
        constexpr uint32_t capacity = DefaultSlabSize / sizeof(Value) + 1;
        HeapSpace old;
        HeapSpace non_moving;
        HeapObject *root;
        {
            HeapSpace nursery;
            std::array<OverflowSlots *, 4> objects;
            for(auto &object: objects)
            {
                object =
                    make_slots(nursery, capacity, HeapGenerationState::Young);
                EXPECT_EQ(0u, object->get_size());
            }
            auto *empty = make_slots(nursery, 0, HeapGenerationState::Young);
            for(size_t index = 0; index < objects.size(); ++index)
            {
                connect(objects[index], capacity - 1,
                        objects[(index + 1) % objects.size()]);
            }
            connect(objects[0], 0, empty);
            root = objects[0];
            std::array spaces{
                EvacuationSpace{&nursery, HeapGenerationState::Young}};
            EvacuationSpaces inputs(spaces);
            EvacuationDestinations destinations(old, non_moving);
            auto placement = [&](HeapObject *object) {
                return object == objects[1] || object == objects[3]
                           ? Destination::NonMoving
                           : Destination::Old;
            };
            RealLayoutTestAdapter<CollectionKind::Young, decltype(placement)>
                adapter(inputs, destinations, placement);
            Evacuation collection(adapter);
            collection.add_root(HeapReferenceSlot(&root));
            collection.finish();
            ASSERT_EQ(5u, adapter.copies().size());
            adapter.expect_canonical_copies();
            for(const HeapSpace *space: {&old, &non_moving})
            {
                size_t mappings = 0;
                space->visit_memory_ranges(
                    [&](uintptr_t, uintptr_t) { ++mappings; });
                EXPECT_GE(mappings, 2u);
            }
        }
        auto *current = static_cast<OverflowSlots *>(root);
        auto *empty = static_cast<OverflowSlots *>(edge(current, 0));
        EXPECT_EQ(0u, empty->get_capacity());
        for(size_t index = 0; index < 4; ++index)
        {
            EXPECT_EQ(capacity, current->get_capacity());
            EXPECT_EQ(0u, current->get_size());
            EXPECT_EQ(index % 2 != 0, current->is_non_moving());
            current = static_cast<OverflowSlots *>(edge(current, capacity - 1));
        }
        EXPECT_EQ(root, current);
    }
}  // namespace
