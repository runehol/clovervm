#ifndef CL_GC_TEST_HEAP_H
#define CL_GC_TEST_HEAP_H

#include "gc/weak_repair.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace cl::gc::test
{
    struct TestObject;

    struct TestCell
    {
        std::variant<int64_t, TestObject *> contents;

        static TestCell immediate(int64_t value) { return TestCell{value}; }
        static TestCell reference(TestObject *object)
        {
            return TestCell{object};
        }

        bool operator==(const TestCell &) const = default;
    };

    // IC-like unit: two guards and an active tagged payload share one reset.
    // Counters are test instrumentation, not part of the queued unit handle.
    struct TestWeakEntry
    {
        std::array<TestObject *, 2> guards;
        TestCell payload;
        size_t repair_count = 0;
        size_t reset_count = 0;

        static TestWeakEntry active(TestObject *first, TestObject *second,
                                    TestCell payload)
        {
            return {{first, second}, payload};
        }

        void clear()
        {
            guards.fill(nullptr);
            payload = TestCell::immediate(0);
            ++reset_count;
        }

        bool operator==(const TestWeakEntry &) const = default;
    };

    inline WeakState repair_test_weak_target(TestObject *&target,
                                             WeakResolver<TestObject> resolver)
    {
        if(target == nullptr)
        {
            return WeakState::Resolved;
        }
        WeakResolution<TestObject> resolution = resolver(target);
        switch(resolution.state)
        {
            case WeakState::Resolved:
                target = resolution.target;
                break;
            case WeakState::Pending:
                break;
            case WeakState::Dead:
                target = nullptr;
                break;
        }
        return resolution.state;
    }

    inline WeakRepairResult
    repair_test_weak_field(TestObject *&target,
                           WeakResolver<TestObject> resolver)
    {
        return repair_test_weak_target(target, resolver) == WeakState::Pending
                   ? WeakRepairResult::Pending
                   : WeakRepairResult::Complete;
    }

    inline WeakRepairResult
    repair_test_weak_entry(TestWeakEntry &entry,
                           WeakResolver<TestObject> resolver)
    {
        ++entry.repair_count;
        if(entry.guards[0] == nullptr || entry.guards[1] == nullptr)
        {
            entry.clear();
            return WeakRepairResult::Complete;
        }
        bool pending = false;
        bool dead = false;
        auto inspect = [&](TestObject *&target) {
            WeakState state = repair_test_weak_target(target, resolver);
            pending |= state == WeakState::Pending;
            dead |= state == WeakState::Dead;
        };
        for(TestObject *&guard: entry.guards)
        {
            inspect(guard);
        }
        if(auto payload = std::get_if<TestObject *>(&entry.payload.contents))
        {
            inspect(*payload);
        }
        if(dead)
        {
            entry.clear();
            return WeakRepairResult::Complete;
        }
        return pending ? WeakRepairResult::Pending : WeakRepairResult::Complete;
    }

    enum class TestHeaderKind : uint8_t
    {
        Normal,
        Forwarded,
    };

    struct TestHeader
    {
        TestHeaderKind kind;
        TestObject *target;

        static TestHeader normal() { return {TestHeaderKind::Normal, nullptr}; }
        static TestHeader forwarded(TestObject *target)
        {
            assert(target != nullptr);
            return {TestHeaderKind::Forwarded, target};
        }
    };

    enum class TestStorage : uint8_t
    {
        Source,
        NonMoving,
        OrdinaryDestination,
        StableDestination,
    };

    // Placement policy belongs to the test adapter, not the collector API.
    enum class EvacuationDestination : uint8_t
    {
        Ordinary,
        Stable,
    };

    enum class TestGeneration : uint8_t
    {
        Young,
        Old,
    };

    // Full participation includes all non-moving objects, including fresh
    // copies, as well as young and old movable sources. Sweeping is separate.
    enum class TestCollectionKind : uint8_t
    {
        YoungOnly,
        Full,
    };

    struct TestObject
    {
        uint64_t logical_identity;
        TestHeader header = TestHeader::normal();
        // Test-heap allocation metadata survives source header forwarding.
        TestStorage storage = TestStorage::Source;
        TestGeneration generation = TestGeneration::Young;
        EvacuationDestination preferred_destination =
            EvacuationDestination::Ordinary;
        std::vector<TestCell> strong_cells;
        std::vector<TestObject *> weak_targets;
        std::vector<TestWeakEntry> weak_entries;
    };

    class TestSlot
    {
    public:
        explicit TestSlot(TestCell &cell) : cell_(&cell) {}

        TestObject *load_pointer() const
        {
            auto pointer = std::get_if<TestObject *>(&cell_->contents);
            return pointer == nullptr ? nullptr : *pointer;
        }

        void rewrite_pointer(TestObject *replacement) const
        {
            assert(load_pointer() != nullptr && replacement != nullptr);
            cell_->contents = replacement;
        }

    private:
        TestCell *cell_;
    };

    // Finish graph construction before marking. unique_ptr keeps object
    // addresses stable even when the owning vector grows; cell vectors and
    // root storage must not resize during traversal.
    class TestHeap
    {
        // Fixed-size test records are placement-constructed in a typed arena.
        // Slab ownership, not the collector worklist, handles native teardown.
        class DestinationChunk
        {
        public:
            explicit DestinationChunk(size_t capacity)
                : capacity_(capacity),
                  data_(std::allocator<TestObject>{}.allocate(capacity))
            {
            }

            ~DestinationChunk()
            {
                for(size_t idx = 0; idx < size_; ++idx)
                {
                    std::destroy_at(data_ + idx);
                }
                std::allocator<TestObject>{}.deallocate(data_, capacity_);
            }

            DestinationChunk(const DestinationChunk &) = delete;
            DestinationChunk &operator=(const DestinationChunk &) = delete;

            size_t remaining() const { return capacity_ - size_; }
            size_t size() const { return size_; }

            TestObject *copy(TestObject *source, TestStorage storage)
            {
                assert(remaining() != 0);
                TestObject *destination =
                    std::construct_at(data_ + size_, *source);
                ++size_;
                destination->header = TestHeader::normal();
                destination->storage = storage;
                destination->generation = TestGeneration::Old;
                return destination;
            }

        private:
            size_t capacity_;
            TestObject *data_;
            size_t size_ = 0;
        };

        struct DestinationArena
        {
            std::vector<std::unique_ptr<DestinationChunk>> chunks;
            size_t chunk_limit = std::numeric_limits<size_t>::max();
        };

    public:
        explicit TestHeap(size_t destination_chunk_capacity = 64)
            : destination_chunk_capacity_(destination_chunk_capacity)
        {
            assert(destination_chunk_capacity != 0);
        }

        TestObject *make_object()
        {
            auto object = std::make_unique<TestObject>();
            object->logical_identity = next_identity_++;
            TestObject *result = object.get();
            objects_.push_back(std::move(object));
            return result;
        }

        TestObject *make_non_moving_object()
        {
            TestObject *object = make_object();
            object->storage = TestStorage::NonMoving;
            object->generation = TestGeneration::Old;
            return object;
        }

        TestObject *copy_object(TestObject *source)
        {
            assert(source != nullptr);
            assert(source->storage == TestStorage::Source);
            assert(source->header.kind == TestHeaderKind::Normal);
            try
            {
                EvacuationDestination destination =
                    source->preferred_destination;
                DestinationArena &arena = arena_for(destination);
                if(arena.chunks.empty() ||
                   arena.chunks.back()->remaining() == 0)
                {
                    if(arena.chunks.size() >= arena.chunk_limit)
                    {
                        allocation_failure();
                    }
                    arena.chunks.push_back(std::make_unique<DestinationChunk>(
                        destination_chunk_capacity_));
                }
                TestStorage storage =
                    destination == EvacuationDestination::Ordinary
                        ? TestStorage::OrdinaryDestination
                        : TestStorage::StableDestination;
                return arena.chunks.back()->copy(source, storage);
            }
            catch(const std::bad_alloc &)
            {
                allocation_failure();
            }
        }

        void limit_destination_chunks(EvacuationDestination destination,
                                      size_t maximum)
        {
            arena_for(destination).chunk_limit = maximum;
        }

        size_t destination_chunk_count(EvacuationDestination destination) const
        {
            return arena_for(destination).chunks.size();
        }

        size_t destination_object_count(EvacuationDestination destination) const
        {
            size_t result = 0;
            for(const auto &chunk: arena_for(destination).chunks)
            {
                result += chunk->size();
            }
            return result;
        }

        [[noreturn]] static void allocation_failure()
        {
            std::fputs("test GC allocation failed\n", stderr);
            std::abort();
        }

        std::span<const std::unique_ptr<TestObject>> objects() const
        {
            return objects_;
        }

        // Test-only lifecycle boundary. The caller must finish strong and weak
        // repair first, and stop using source objects and source slot handles.
        void discard_source_storage(TestCollectionKind kind)
        {
            std::erase_if(objects_, [&](const auto &object) {
                return object->storage == TestStorage::Source &&
                       (kind == TestCollectionKind::Full ||
                        object->generation == TestGeneration::Young);
            });
        }

    private:
        DestinationArena &arena_for(EvacuationDestination destination)
        {
            switch(destination)
            {
                case EvacuationDestination::Ordinary:
                    return ordinary_;
                case EvacuationDestination::Stable:
                    return stable_;
            }
            std::abort();
        }

        const DestinationArena &
        arena_for(EvacuationDestination destination) const
        {
            return const_cast<TestHeap *>(this)->arena_for(destination);
        }

        uint64_t next_identity_ = 0;
        std::vector<std::unique_ptr<TestObject>> objects_;
        size_t destination_chunk_capacity_;
        DestinationArena ordinary_;
        DestinationArena stable_;
    };

    class TestHeapAdapter
    {
    public:
        using Object = TestObject;
        using Slot = TestSlot;
        using WeakUnit = WeakRepairUnit<Object>;

        TestHeapAdapter(TestHeap &heap, TestCollectionKind collection_kind)
            : heap_(heap), collection_kind_(collection_kind)
        {
        }

        bool is_collected(Object *object) const
        {
            switch(object->storage)
            {
                case TestStorage::Source:
                    return collection_kind_ == TestCollectionKind::Full ||
                           object->generation == TestGeneration::Young;
                case TestStorage::NonMoving:
                case TestStorage::StableDestination:
                    return collection_kind_ == TestCollectionKind::Full;
                case TestStorage::OrdinaryDestination:
                    return false;
            }
            std::abort();
        }

        bool is_movable(Object *object) const
        {
            switch(object->storage)
            {
                case TestStorage::Source:
                case TestStorage::OrdinaryDestination:
                    return true;
                case TestStorage::NonMoving:
                case TestStorage::StableDestination:
                    return false;
            }
            std::abort();
        }

        Object *copy_object(Object *source)
        {
            assert(is_collected(source) && is_movable(source));
            return heap_.copy_object(source);
        }

        Object *forwarded_to(Object *source) const
        {
            assert(is_collected(source) && is_movable(source));
            return source->header.kind == TestHeaderKind::Forwarded
                       ? source->header.target
                       : nullptr;
        }

        void install_forwarding(Object *source, Object *destination)
        {
            assert(is_collected(source) && is_movable(source));
            assert(source->header.kind == TestHeaderKind::Normal);
            assert(destination != nullptr && destination != source);
            assert(destination->storage == TestStorage::OrdinaryDestination ||
                   destination->storage == TestStorage::StableDestination);
            assert(destination->header.kind == TestHeaderKind::Normal);
            assert(source->logical_identity == destination->logical_identity);
            source->header = TestHeader::forwarded(destination);
        }

        [[noreturn]] void allocation_failure() const
        {
            TestHeap::allocation_failure();
        }

        bool try_mark(Object *object)
        {
            assert(object != nullptr);
            return marked_.insert(object).second;
        }

        bool is_marked(Object *object) const
        {
            return marked_.contains(object);
        }

        template <typename Visitor>
        void visit_strong_slots(Object *object, Visitor &&visitor)
        {
            ++scan_counts_[object];
            for(TestCell &cell: object->strong_cells)
            {
                std::invoke(visitor, Slot(cell));
            }
        }

        template <typename Visitor>
        void visit_weak_units(Object *object, Visitor &&visitor)
        {
            ++weak_scan_counts_[object];
            for(Object *&target: object->weak_targets)
            {
                std::invoke(visitor,
                            WeakUnit::bind<repair_test_weak_field>(target));
            }
            for(TestWeakEntry &entry: object->weak_entries)
            {
                std::invoke(visitor,
                            WeakUnit::bind<repair_test_weak_entry>(entry));
            }
        }

        size_t weak_scan_count(Object *object) const
        {
            auto count = weak_scan_counts_.find(object);
            return count == weak_scan_counts_.end() ? 0 : count->second;
        }

        size_t scan_count(Object *object) const
        {
            auto count = scan_counts_.find(object);
            return count == scan_counts_.end() ? 0 : count->second;
        }

    private:
        TestHeap &heap_;
        TestCollectionKind collection_kind_;
        absl::flat_hash_set<Object *> marked_;
        absl::flat_hash_map<Object *, size_t> scan_counts_;
        absl::flat_hash_map<Object *, size_t> weak_scan_counts_;
    };
}  // namespace cl::gc::test

#endif  // CL_GC_TEST_HEAP_H
