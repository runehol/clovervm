#include "memory/heap_object_copy.h"

#include "memory/heap_space.h"
#include "memory/native_layout_descriptor.h"

#include <cassert>
#include <cstring>

namespace cl
{
    HeapObject *copy_object_bytes(HeapSpace *destination,
                                  const HeapObject *source)
    {
        assert(destination != nullptr);
        assert(source != nullptr);
        assert(!source->is_forwarded());
        NativeLayoutInfo layout = native_layout_info(source);
        std::byte *copy = destination->allocate(layout.allocated_size);
        std::memcpy(copy, source, layout.copy_size);
        return reinterpret_cast<HeapObject *>(copy);
    }
}  // namespace cl
