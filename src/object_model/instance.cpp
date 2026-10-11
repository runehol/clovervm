#include "object_model/instance.h"
#include "object_model/class_object.h"
#include "object_model/shape.h"
#include "runtime/virtual_machine.h"
#include <cassert>
#include <cstdint>

namespace cl
{

    Instance::Instance(ClassObject *_cls)
        : InlineSlotObject(_cls, native_layout)
    {
        assert(inline_slot_count_for_class(_cls) <= UINT16_MAX);
        (void)_cls;
    }

    uint32_t Instance::inline_slot_count_for_class(ClassObject *cls)
    {
        assert(cls->instance_native_layout_id() == native_layout);
        Shape *shape = cls->get_instance_root_shape();
        assert(cls->get_instance_inline_slot_capacity() ==
               shape->get_inline_slot_capacity());
        return shape->get_inline_slot_capacity();
    }

}  // namespace cl
