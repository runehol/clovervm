#ifndef CL_GC_EVACUATION_H
#define CL_GC_EVACUATION_H

#include "gc/weak_repair.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <new>

namespace cl::gc
{
    // One copying phase. The caller supplies roots and remembered slots,
    // excludes mutator activity/safepoints, and keeps source storage alive.
    // Slot decoding, placement and copying belong to the adapter.
    // Outside-source objects are not scanned implicitly. finish() closes strong
    // tracing and repairs deferred weak units before source reclamation by the
    // caller. All supplied weak units must have unique, non-overlapping repair
    // storage that remains valid through finish(). Allocation failure is fatal.
    template <typename Heap> class Evacuation
    {
        using Object = typename Heap::Object;
        using Slot = typename Heap::Slot;
        using WeakUnit = typename Heap::WeakUnit;

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
            assert(phase_ == Phase::Tracing);
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
            assert(phase_ == Phase::Tracing);
            try
            {
                while(!pending_.empty())
                {
                    Object *object = pending_.front();
                    pending_.pop_front();
                    heap_.visit_strong_slots(
                        object, [&](Slot slot) { evacuate_slot(slot); });
                    heap_.visit_weak_units(
                        object, [&](WeakUnit unit) { repair_early(unit); });
                }
            }
            catch(const std::bad_alloc &)
            {
                heap_.allocation_failure();
            }
        }

        // Explicit weak roots and weak-remembered units use the same protocol
        // as units discovered in copied objects; neither retains its targets.
        void add_weak_root(WeakUnit unit)
        {
            assert(phase_ == Phase::Tracing);
            try
            {
                repair_early(unit);
            }
            catch(const std::bad_alloc &)
            {
                heap_.allocation_failure();
            }
        }

        void finish()
        {
            drain();
            phase_ = Phase::RepairingWeak;
            try
            {
                while(!weak_pending_.empty())
                {
                    WeakUnit unit = weak_pending_.front();
                    weak_pending_.pop_front();
                    [[maybe_unused]] WeakRepairResult result =
                        unit.try_repair([&](Object *target) {
                            return resolve_weak(target, WeakPhase::Late);
                        });
                    assert(result == WeakRepairResult::Complete);
                }
            }
            catch(const std::bad_alloc &)
            {
                heap_.allocation_failure();
            }
            phase_ = Phase::Finished;
        }

    private:
        enum class Phase : uint8_t
        {
            Tracing,
            RepairingWeak,
            Finished,
        };

        enum class WeakPhase : uint8_t
        {
            Early,
            Late,
        };

        WeakResolution<Object> resolve_weak(Object *target, WeakPhase phase)
        {
            assert(target != nullptr);
            if(!heap_.is_collected_source(target))
            {
                return WeakResolution<Object>::resolved(target);
            }
            if(Object *destination = heap_.forwarded_to(target))
            {
                return WeakResolution<Object>::resolved(destination);
            }
            return phase == WeakPhase::Early ? WeakResolution<Object>::pending()
                                             : WeakResolution<Object>::dead();
        }

        void repair_early(WeakUnit unit)
        {
            if(unit.try_repair([&](Object *target) {
                   return resolve_weak(target, WeakPhase::Early);
               }) == WeakRepairResult::Pending)
            {
                weak_pending_.push_back(unit);
            }
        }

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

            Object *destination = heap_.copy_object(source);
            heap_.install_forwarding(source, destination);
            pending_.push_back(destination);
            slot.rewrite_pointer(destination);
        }

        Heap &heap_;
        std::deque<Object *> pending_;
        std::deque<WeakUnit> weak_pending_;
        Phase phase_ = Phase::Tracing;
    };
}  // namespace cl::gc

#endif  // CL_GC_EVACUATION_H
