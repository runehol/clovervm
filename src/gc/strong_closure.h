#ifndef CL_GC_STRONG_CLOSURE_H
#define CL_GC_STRONG_CLOSURE_H

#include <deque>

namespace cl::gc
{
    // One mark phase: use a fresh adapter, add all roots, then drain once.
    // Roots and objects are borrowed; the caller must exclude graph mutation,
    // reclamation and safepoints for the entire phase. Native scratch failure
    // requires discarding both this worklist and the adapter's partial marks.
    template <typename Heap> class StrongClosure
    {
        using Object = typename Heap::Object;
        using Slot = typename Heap::Slot;

    public:
        explicit StrongClosure(Heap &heap) : heap_(heap) {}

        void add_root(Slot slot) { discover(slot.load_pointer()); }

        void drain()
        {
            while(!pending_.empty())
            {
                Object *object = pending_.front();
                pending_.pop_front();
                heap_.visit_strong_slots(
                    object, [&](Slot slot) { discover(slot.load_pointer()); });
            }
        }

    private:
        void discover(Object *object)
        {
            if(object != nullptr && heap_.try_mark(object))
            {
                pending_.push_back(object);
            }
        }

        Heap &heap_;
        std::deque<Object *> pending_;
    };
}  // namespace cl::gc

#endif  // CL_GC_STRONG_CLOSURE_H
