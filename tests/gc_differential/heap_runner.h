#ifndef CL_GC_DIFFERENTIAL_HEAP_RUNNER_H
#define CL_GC_DIFFERENTIAL_HEAP_RUNNER_H

#include "gc_test_heap.h"
#include "heap_format.h"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace cl::gc::test::differential
{
    enum class Algorithm
    {
        Mark,
        Evacuate
    };

    class RunnerError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // One full collection, retaining all original storage in the result.
    // Invalid input throws FormatError; inconsistent native state throws
    // RunnerError. Evacuation scratch/allocation failure remains fatal under
    // the existing collector's noexcept contract.
    format::HeapSnapshot run_collection(const format::HeapSnapshot &input,
                                        Algorithm algorithm);

    // Internal test support, also exposed here for capture fault-injection
    // tests.
    namespace detail
    {
        struct AllocationRecord
        {
            TestObject *address;
            format::AllocationId original_allocation;
        };
        class AllocationRegistry
        {
        public:
            void add_original(TestObject *object);
            void add_copy(TestObject *source, TestObject *copy);
            format::AllocationId id(TestObject *object) const;
            TestObject &object(format::AllocationId id) const;
            std::span<const AllocationRecord> records() const
            {
                return records_;
            }

        private:
            void append(TestObject *object, format::AllocationId original);
            std::vector<AllocationRecord> records_;
            absl::flat_hash_map<TestObject *, format::AllocationId> ids_;
        };
        class RecordingAdapter : public TestHeapAdapter
        {
        public:
            RecordingAdapter(TestHeap &heap, AllocationRegistry &allocations)
                : TestHeapAdapter(heap, TestCollectionKind::Full),
                  allocations_(allocations)
            {
            }
            TestObject *copy_object(TestObject *source);

        private:
            AllocationRegistry &allocations_;
        };

        using NativeWeak = std::variant<TestCell, TestWeakEntry>;
        class NativeHeap
        {
        public:
            explicit NativeHeap(const format::HeapSnapshot &input);
            NativeHeap(const NativeHeap &) = delete;
            NativeHeap &operator=(const NativeHeap &) = delete;
            NativeHeap(NativeHeap &&) = delete;
            NativeHeap &operator=(NativeHeap &&) = delete;

            void collect(Algorithm algorithm);
            format::HeapSnapshot capture() const;
            TestObject &object(format::AllocationId id)
            {
                return allocations_.object(id);
            }
            RecordingAdapter &adapter() { return adapter_; }
            std::span<TestCell> roots() { return roots_; }
            std::span<NativeWeak> external_weak() { return external_weak_; }

        private:
            enum class Phase
            {
                Fresh,
                Running,
                Finished,
                Failed
            };
            TestCell make_cell(const format::Cell &cell) const;
            TestWeakEntry make_entry(const format::Entry &entry) const;
            format::Cell capture_cell(const TestCell &cell) const;
            format::Entry capture_entry(const TestWeakEntry &entry) const;
            format::ObjectState capture_state(const TestObject &object,
                                              const AllocationRecord &record,
                                              format::AllocationId id) const;

            format::HeapSnapshot
                input_;  // Sorted originals; immutable layout metadata.
            TestHeap heap_;
            AllocationRegistry allocations_;
            RecordingAdapter adapter_;
            std::vector<TestCell> roots_;
            std::vector<NativeWeak> external_weak_;
            Phase phase_ = Phase::Fresh;
        };
    }  // namespace detail
}  // namespace cl::gc::test::differential

#endif
