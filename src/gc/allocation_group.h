#ifndef CL_GC_ALLOCATION_GROUP_H
#define CL_GC_ALLOCATION_GROUP_H

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace cl::gc
{
    template <typename Object> struct AllocationGroup
    {
        std::array<Object *, 2> members;
        size_t count;

        static AllocationGroup single(Object *object)
        {
            assert(object != nullptr);
            return {{object, nullptr}, 1};
        }

        static AllocationGroup pair(Object *primary, Object *companion)
        {
            assert(primary != nullptr && companion != nullptr);
            assert(primary != companion);
            return {{primary, companion}, 2};
        }
    };

    enum class EvacuationDestination : uint8_t
    {
        Ordinary,
        Stable,
    };
}  // namespace cl::gc

#endif  // CL_GC_ALLOCATION_GROUP_H
