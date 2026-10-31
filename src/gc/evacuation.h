#ifndef CL_GC_EVACUATION_H
#define CL_GC_EVACUATION_H

#include <cstddef>
#include <deque>
#include <new>

namespace cl::gc
{
    // One strong copying phase. The caller supplies roots and remembered slots,
    // excludes mutator activity/safepoints, and keeps source storage alive.
    // Slot decoding, placement and copying belong to the adapter.
    // Outside-source objects are not scanned implicitly. Weak repair and
    // reclamation are later phases; allocation failure must never return to the
    // mutator.
    template <typename Heap> class Evacuation
    {
        using Object = typename Heap::Object;
        using Slot = typename Heap::Slot;

    public:
        explicit Evacuation(Heap &heap)
        try : heap_(heap) {}
        catch(const std::bad_alloc &)
        {
            // deque construction itself may allocate. Use the parameter, not a
            // member of the object whose construction has already unwound.
            heap.allocation_failure();
        }

        Evacuation(const Evacuation &) = delete;
        Evacuation &operator=(const Evacuation &) = delete;

        void add_root(Slot slot)
        {
            try
            {
                evacuate_slot(slot);
            }
            catch(const std::bad_alloc &)
            {
                heap_.allocation_failure();
            }
        }

        void drain()
        {
            try
            {
                while(!pending_.empty())
                {
                    Object *object = pending_.front();
                    pending_.pop_front();
                    heap_.visit_strong_slots(
                        object, [&](Slot slot) { evacuate_slot(slot); });
                }
            }
            catch(const std::bad_alloc &)
            {
                heap_.allocation_failure();
            }
        }

    private:
        void evacuate_slot(Slot slot)
        {
            Object *source = slot.load_pointer();
            if(source == nullptr || !heap_.is_collected_source(source))
            {
                return;
            }
            if(Object *destination = heap_.forwarded_to(source))
            {
                slot.rewrite_pointer(destination);
                return;
            }

            auto group = heap_.allocation_group(source);
            auto destinations =
                heap_.copy_group(group, heap_.destination_for(group));
            // Forward every member before scanning any, including companions.
            for(size_t idx = 0; idx < group.count; ++idx)
            {
                heap_.install_forwarding(group.members[idx], destinations[idx]);
            }
            for(size_t idx = 0; idx < group.count; ++idx)
            {
                pending_.push_back(destinations[idx]);
            }
            slot.rewrite_pointer(destinations[0]);
        }

        Heap &heap_;
        std::deque<Object *> pending_;
    };
}  // namespace cl::gc

#endif  // CL_GC_EVACUATION_H
