#ifndef CL_GC_TEST_HEAP_H
#define CL_GC_TEST_HEAP_H

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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

    struct TestObject
    {
        uint64_t logical_identity;
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

    private:
        TestCell *cell_;
    };

    // Finish graph construction before marking. unique_ptr keeps object
    // addresses stable even when the owning vector grows; cell vectors and
    // root storage must not resize during traversal.
    class TestHeap
    {
    public:
        TestObject *make_object()
        {
            auto object = std::make_unique<TestObject>();
            object->logical_identity = next_identity_++;
            TestObject *result = object.get();
            objects_.push_back(std::move(object));
            return result;
        }

        std::span<const std::unique_ptr<TestObject>> objects() const
        {
            return objects_;
        }

    private:
        uint64_t next_identity_ = 0;
        std::vector<std::unique_ptr<TestObject>> objects_;
    };

    class TestHeapAdapter
    {
    public:
        using Object = TestObject;
        using Slot = TestSlot;

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

        size_t scan_count(Object *object) const
        {
            auto count = scan_counts_.find(object);
            return count == scan_counts_.end() ? 0 : count->second;
        }

    private:
        absl::flat_hash_set<Object *> marked_;
        absl::flat_hash_map<Object *, size_t> scan_counts_;
    };
}  // namespace cl::gc::test

#endif  // CL_GC_TEST_HEAP_H
