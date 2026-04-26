#include "instance.h"
#include "class_object.h"
#include "shape.h"
#include "virtual_machine.h"

namespace cl
{

    Instance::Instance(HeapLayout layout, ClassObject *_cls)
        : Object(_cls, native_layout_id, layout)
    {
    }

    DynamicLayoutSpec Instance::layout_spec_for(ClassObject *cls)
    {
        Shape *shape = cls->get_instance_root_shape();
        uint32_t instance_default_inline_slot_count =
            shape->get_instance_default_inline_slot_count();
        assert(instance_default_inline_slot_count >= 1);
        uint32_t dynamic_inline_slot_count =
            instance_default_inline_slot_count - 1;
        return DynamicLayoutSpec{
            round_up_to_16byte_units(size_for(dynamic_inline_slot_count)),
            instance_default_inline_slot_count};
    }

}  // namespace cl
