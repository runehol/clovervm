#ifndef CL_INSTANCE_H
#define CL_INSTANCE_H

#include "builtin_class_registry.h"
#include "object.h"
#include "value.h"
#include <cstdint>

namespace cl
{
    class VirtualMachine;

    class Instance : public SlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::Instance;

        explicit Instance(ClassObject *cls);

        static size_t size_for(uint32_t dynamic_inline_slot_count)
        {
            return sizeof(Instance) + sizeof(Value) * dynamic_inline_slot_count;
        }
        static size_t size_for(ClassObject *cls)
        {
            return size_for(inline_slot_count_for_class(cls));
        }
        static size_t object_size_in_bytes(const Instance *instance)
        {
            return size_for(instance->native_layout_aux_count_value());
        }

        static uint32_t inline_slot_count_for_class(ClassObject *cls);

    public:
        CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN_EXTENDS(Instance, SlotObject, 0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(Instance, Instance::object_size_in_bytes);
    };

}  // namespace cl

#endif  // CL_INSTANCE_H
