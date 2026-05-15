#include "instance.h"
#include "class_object.h"
#include "shape.h"
#include "virtual_machine.h"
#include <cassert>
#include <cstdint>

namespace cl
{

    Instance::Instance(ClassObject *_cls) : SlotObject(_cls, native_layout)
    {
        assert(inline_slot_count_for_class(_cls) <= UINT16_MAX);
        (void)_cls;
    }

    uint32_t Instance::inline_slot_count_for_class(ClassObject *cls)
    {
        Shape *shape = cls->get_instance_root_shape();
        return shape->get_instance_default_inline_slot_count();
    }

}  // namespace cl
