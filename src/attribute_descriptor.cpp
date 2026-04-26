#include "attribute_descriptor.h"

#include "class_object.h"
#include "function.h"

namespace cl
{
    AttributeReadAccessKind
    attribute_read_access_kind_for_path(AttributeReadAccessPath path,
                                        Value value)
    {
        if(path == AttributeReadAccessPath::ReceiverOwnProperty)
        {
            return AttributeReadAccessKind::ReceiverSlot;
        }

        if(path == AttributeReadAccessPath::InstanceClassChain &&
           can_convert_to<Function>(value))
        {
            return AttributeReadAccessKind::BindFunctionReceiver;
        }

        return AttributeReadAccessKind::ResolvedValue;
    }

    AttributeCacheBlockers attribute_cache_blockers_for_class_value(Value value)
    {
        if(!value.is_ptr())
        {
            return attribute_cache_blocker(AttributeCacheBlocker::None);
        }

        Object *object = value.get_ptr<Object>();
        Shape *type_shape = object->get_class().extract()->get_shape();
        if(type_shape->has_flag(ShapeFlag::IsImmutableType))
        {
            return attribute_cache_blocker(AttributeCacheBlocker::None);
        }

        return attribute_cache_blocker(
            AttributeCacheBlocker::MutableDescriptorType);
    }
}  // namespace cl
