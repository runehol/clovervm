#ifndef CL_HEAP_CONSTANTS_H
#define CL_HEAP_CONSTANTS_H

#include <cstddef>
#include <cstdint>

namespace cl
{
    static constexpr uintptr_t SlabLookupGranuleShift = 12;
    static constexpr size_t SlabLookupGranuleSize = size_t(1)
                                                    << SlabLookupGranuleShift;
    static constexpr size_t DefaultSlabSize = 65536;
    static constexpr size_t LargeAllocationSize = DefaultSlabSize / 2;
}  // namespace cl

#endif  // CL_HEAP_CONSTANTS_H
