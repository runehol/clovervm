#ifndef CL_GC_TEST_HEAP_H
#define CL_GC_TEST_HEAP_H

#include "gc/allocation_group.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
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
        OrdinaryDestination,
        StableDestination,
    };

    struct TestObject
    {
        uint64_t logical_identity;
        TestHeader header = TestHeader::normal();
        // Test-heap allocation metadata survives source header forwarding.
        TestStorage storage = TestStorage::Source;
        EvacuationDestination preferred_destination =
            EvacuationDestination::Ordinary;
        TestObject *exclusive_owner = nullptr;  // Not a traced edge.
        TestCell companion = TestCell::reference(nullptr);
        std::vector<TestCell> strong_cells;
        std::vector<TestObject *> weak_targets;
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
                destination->exclusive_owner = nullptr;
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

        AllocationGroup<TestObject> make_pair()
        {
            TestObject *primary = make_object();
            TestObject *companion = make_object();
            primary->companion = TestCell::reference(companion);
            companion->exclusive_owner = primary;
            return AllocationGroup<TestObject>::pair(primary, companion);
        }

        std::array<TestObject *, 2>
        copy_group(const AllocationGroup<TestObject> &group,
                   EvacuationDestination destination)
        {
            assert(group.count == 1 || group.count == 2);
            for(size_t idx = 0; idx < group.count; ++idx)
            {
                assert(group.members[idx] != nullptr);
                assert(group.members[idx]->storage == TestStorage::Source);
                assert(group.members[idx]->header.kind ==
                       TestHeaderKind::Normal);
            }
            try
            {
                DestinationArena &arena = arena_for(destination);
                if(arena.chunks.empty() ||
                   arena.chunks.back()->remaining() < group.count)
                {
                    if(arena.chunks.size() >= arena.chunk_limit)
                    {
                        allocation_failure();
                    }
                    arena.chunks.push_back(std::make_unique<DestinationChunk>(
                        std::max(destination_chunk_capacity_, group.count)));
                }
                TestStorage storage =
                    destination == EvacuationDestination::Ordinary
                        ? TestStorage::OrdinaryDestination
                        : TestStorage::StableDestination;
                std::array<TestObject *, 2> result = {nullptr, nullptr};
                for(size_t idx = 0; idx < group.count; ++idx)
                {
                    result[idx] =
                        arena.chunks.back()->copy(group.members[idx], storage);
                }
                if(group.count == 2)
                {
                    result[1]->exclusive_owner = result[0];
                }
                return result;
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
        using Group = AllocationGroup<Object>;

        explicit TestHeapAdapter(TestHeap &heap) : heap_(heap) {}

        bool is_collected_source(Object *object) const
        {
            return object->storage == TestStorage::Source;
        }

        Group allocation_group(Object *source) const
        {
            assert(is_collected_source(source));
            assert(source->header.kind == TestHeaderKind::Normal);
            assert(source->exclusive_owner == nullptr);
            Object *companion = Slot(source->companion).load_pointer();
            if(companion != nullptr)
            {
                assert(companion->exclusive_owner == source);
                assert(is_collected_source(companion));
                assert(Slot(companion->companion).load_pointer() == nullptr);
                assert(companion->header.kind == TestHeaderKind::Normal);
                return Group::pair(source, companion);
            }
            return Group::single(source);
        }

        EvacuationDestination destination_for(const Group &group) const
        {
            for(size_t idx = 0; idx < group.count; ++idx)
            {
                if(group.members[idx]->preferred_destination ==
                   EvacuationDestination::Stable)
                {
                    return EvacuationDestination::Stable;
                }
            }
            return EvacuationDestination::Ordinary;
        }

        std::array<Object *, 2> copy_group(const Group &group,
                                           EvacuationDestination destination)
        {
            return heap_.copy_group(group, destination);
        }

        Object *forwarded_to(Object *source) const
        {
            assert(is_collected_source(source));
            return source->header.kind == TestHeaderKind::Forwarded
                       ? source->header.target
                       : nullptr;
        }

        void install_forwarding(Object *source, Object *destination)
        {
            assert(is_collected_source(source));
            assert(source->header.kind == TestHeaderKind::Normal);
            assert(destination != nullptr && !is_collected_source(destination));
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
            std::invoke(visitor, Slot(object->companion));
            for(TestCell &cell: object->strong_cells)
            {
                std::invoke(visitor, Slot(cell));
            }
        }

        size_t scan_count(Object *object) const
        {
            auto count = scan_counts_.find(object);
            return count == scan_counts_.end() ? 0 : count->second;
        }

    private:
        TestHeap &heap_;
        absl::flat_hash_set<Object *> marked_;
        absl::flat_hash_map<Object *, size_t> scan_counts_;
    };
}  // namespace cl::gc::test

#endif  // CL_GC_TEST_HEAP_H
