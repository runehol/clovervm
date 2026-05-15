#include "native_layout_descriptor.h"

#include <cassert>

namespace cl
{
    size_t object_size_in_bytes(const HeapObject *obj)
    {
        assert(obj != nullptr);
        const ObjectSizeDescriptor &descriptor =
            object_size_descriptor_for(obj->native_layout_id());

        switch(descriptor.kind)
        {
            case ObjectSizeKind::StaticSize:
                return descriptor.static_size_in_bytes;
            case ObjectSizeKind::Custom:
                assert(descriptor.custom_size_in_bytes != nullptr);
                return descriptor.custom_size_in_bytes(obj);
            case ObjectSizeKind::Missing:
            case ObjectSizeKind::DynamicSmiSize:
            case ObjectSizeKind::DynamicAuxSize:
                break;
        }

        assert(false && "object-size descriptor kind not implemented yet");
        return 0;
    }

}  // namespace cl
