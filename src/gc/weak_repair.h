#ifndef CL_GC_WEAK_REPAIR_H
#define CL_GC_WEAK_REPAIR_H

#include <absl/functional/function_ref.h>

#include <cassert>
#include <cstdint>

namespace cl::gc
{
    enum class WeakState : uint8_t
    {
        Resolved,
        Pending,
        Dead,
    };

    enum class WeakRepairResult : uint8_t
    {
        Complete,
        Pending,
    };

    template <typename Object> struct WeakResolution
    {
        WeakState state;
        Object *target;

        static WeakResolution resolved(Object *target)
        {
            assert(target != nullptr);
            return {WeakState::Resolved, target};
        }
        static WeakResolution pending()
        {
            return {WeakState::Pending, nullptr};
        }
        static WeakResolution dead() { return {WeakState::Dead, nullptr}; }
    };

    template <typename Object>
    using WeakResolver = absl::FunctionRef<WeakResolution<Object>(Object *)>;

    // A borrowed address plus one static callback, independent of field count.
    // The callback must not retain the resolver, reenter the collector, or
    // invalidate another queued unit's storage. Late repair must be Complete.
    template <typename Object> struct WeakRepairUnit
    {
        void *address;
        WeakRepairResult (*repair)(void *, WeakResolver<Object>);

        template <auto Repair, typename T> static WeakRepairUnit bind(T &value)
        {
            return {&value, [](void *address, WeakResolver<Object> resolver) {
                        return Repair(*static_cast<T *>(address), resolver);
                    }};
        }

        WeakRepairResult try_repair(WeakResolver<Object> resolver) const
        {
            return repair(address, resolver);
        }
    };
}  // namespace cl::gc

#endif  // CL_GC_WEAK_REPAIR_H
