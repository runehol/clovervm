#include "instance.h"
#include "class_object.h"
#include "shape.h"
#include "virtual_machine.h"
#include <cassert>
#include <cstdint>

namespace cl
{

    Instance::Instance(ClassObject *_cls) : Object(_cls, native_layout)
    {
        uint32_t inline_slot_count = inline_slot_count_for_class(_cls);
        assert(inline_slot_count <= UINT16_MAX);
        set_native_layout_aux_count(static_cast<uint16_t>(inline_slot_count));
    }

    uint32_t Instance::inline_slot_count_for_class(ClassObject *cls)
    {
        Shape *shape = cls->get_instance_root_shape();
        return shape->get_instance_default_inline_slot_count();
    }

}  // namespace cl
