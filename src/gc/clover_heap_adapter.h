#ifndef CL_GC_CLOVER_HEAP_ADAPTER_H
#define CL_GC_CLOVER_HEAP_ADAPTER_H

#include "memory/heap_reference_slot.h"
#include "memory/native_layout_descriptor.h"

#include <absl/container/flat_hash_set.h>

#include <cassert>
#include <functional>

namespace cl::gc
{
    // Per-collection scratch marks, not lifetime authority or owning roots.
    // Strong scanning reuses the existing layout/custom-slot dispatch. No
    // object header, reference count, weak cell or managed reference is
    // changed.
    class CloverHeapAdapter
    {
    public:
        using Object = HeapObject;
        using Slot = HeapReferenceSlot;
        using WeakUnit = cl::WeakRepairUnit;

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
            cl::visit_strong_reference_slots(
                object, [&](Slot slot) { std::invoke(visitor, slot); });
        }

        template <typename Visitor>
        void visit_weak_units(Object *object, Visitor &&visitor)
        {
            cl::visit_weak_reference_units(
                object, [&](WeakUnit unit) { std::invoke(visitor, unit); });
        }

    private:
        absl::flat_hash_set<Object *> marked_;
    };
}  // namespace cl::gc

#endif  // CL_GC_CLOVER_HEAP_ADAPTER_H
