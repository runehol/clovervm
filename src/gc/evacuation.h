#ifndef CL_GC_EVACUATION_H
#define CL_GC_EVACUATION_H

#include "gc/weak_repair.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace cl::gc
{
    // One tracing/copying phase with fresh per-phase adapter marks. The caller
    // supplies roots and remembered slots,
    // excludes mutator activity/safepoints, and keeps source storage alive.
    // Slot decoding, placement and copying belong to the adapter.
    // Only collected inputs are discovered. Mark/enqueue canonical copies or
    // existing non-moving objects, never forwarded source headers.
    // finish() closes strong
    // tracing and repairs deferred weak units before source reclamation by the
    // caller. All supplied weak units must have unique, non-overlapping repair
    // storage that remains valid through finish(). Allocation failure is fatal:
    // null copies use the adapter's failure handler; escaping native exceptions
    // terminate at noexcept entry points. No C++ exception handling is
    // required.
    template <typename Heap> class Evacuation
    {
        using Object = typename Heap::Object;
        using Slot = typename Heap::Slot;
        using WeakUnit = typename Heap::WeakUnit;

    public:
        explicit Evacuation(Heap &heap) noexcept : heap_(heap) {}

        Evacuation(const Evacuation &) = delete;
        Evacuation &operator=(const Evacuation &) = delete;

        void add_root(Slot slot) noexcept
        {
            assert(phase_ == Phase::Tracing);
            discover_slot(slot);
        }

        void drain() noexcept
        {
            assert(phase_ == Phase::Tracing);
            while(!pending_.empty())
            {
                Object *object = pending_.front();
                pending_.pop_front();
                heap_.visit_strong_slots(
                    object, [&](Slot slot) { discover_slot(slot); });
                heap_.visit_weak_units(
                    object, [&](WeakUnit unit) { repair_early(unit); });
            }
        }

        // Explicit weak roots and weak-remembered units use the same protocol
        // as units discovered in copied objects; neither retains its targets.
        void add_weak_root(WeakUnit unit) noexcept
        {
            assert(phase_ == Phase::Tracing);
            repair_early(unit);
        }

        void finish() noexcept
        {
            drain();
            phase_ = Phase::RepairingWeak;
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
            if(!heap_.is_collected(target))
            {
                return WeakResolution<Object>::resolved(target);
            }
            if(heap_.is_collected_source(target))
            {
                if(Object *destination = heap_.forwarded_to(target))
                {
                    return WeakResolution<Object>::resolved(destination);
                }
            }
            else if(heap_.is_marked(target))
            {
                return WeakResolution<Object>::resolved(target);
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

        void enqueue_once(Object *object)
        {
            if(heap_.try_mark(object))
            {
                pending_.push_back(object);
            }
        }

        void discover_slot(Slot slot)
        {
            Object *object = slot.load_pointer();
            if(object == nullptr || !heap_.is_collected(object))
            {
                return;
            }
            if(heap_.is_collected_source(object))
            {
                Object *destination = heap_.forwarded_to(object);
                if(destination == nullptr)
                {
                    destination = heap_.copy_object(object);
                    if(destination == nullptr)
                    {
                        heap_.allocation_failure();
                    }
                    heap_.install_forwarding(object, destination);
                }
                enqueue_once(destination);
                slot.rewrite_pointer(destination);
                return;
            }
            enqueue_once(object);
        }

        Heap &heap_;
        std::deque<Object *> pending_;
        std::deque<WeakUnit> weak_pending_;
        Phase phase_ = Phase::Tracing;
    };
}  // namespace cl::gc

#endif  // CL_GC_EVACUATION_H
