#include "class_object.h"
#include "thread_state.h"

namespace cl
{

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _inline_slot_capacity)
        : Object(&klass, compact_layout()), name(_name),
          inline_slot_capacity(_inline_slot_capacity),
          initial_shape(Value::from_oop(
              ThreadState::get_active()->make_refcounted_raw<Shape>(
                  Value::from_oop(this), Value::None(), 0, 0)))
    {
    }

    Shape *ClassObject::get_initial_shape() const
    {
        return initial_shape.as_value().get_ptr<Shape>();
    }

}  // namespace cl
