#ifndef CL_INSTANCE_H
#define CL_INSTANCE_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/value.h"
#include <cstdint>

namespace cl
{
    class VirtualMachine;

    class Instance : public InlineSlotObject
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;

        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::Instance;

        explicit Instance(ClassObject *cls);

        static size_t allocation_size_for(uint32_t dynamic_inline_slot_count)
        {
            assert(dynamic_inline_slot_count <= UINT16_MAX);
            return sizeof(Instance) + sizeof(Value) * dynamic_inline_slot_count;
        }
        static size_t allocation_size_for(ClassObject *cls)
        {
            return allocation_size_for(inline_slot_count_for_class(cls));
        }
        static size_t object_size_in_bytes(const Instance *instance);
        static size_t copy_size_in_bytes(const Instance *instance)
        {
            return sizeof(Instance) +
                   sizeof(Value) * instance->native_layout_aux_count_value();
        }

        static uint32_t inline_slot_count_for_class(ClassObject *cls);

    public:
        CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN_EXTENDS(Instance, InlineSlotObject,
                                                  0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(Instance, Instance::object_size_in_bytes);
        CL_DECLARE_CUSTOM_COPY_SIZE(Instance, Instance::copy_size_in_bytes);
    };

}  // namespace cl

#endif  // CL_INSTANCE_H
