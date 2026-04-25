#ifndef CL_INSTANCE_H
#define CL_INSTANCE_H

#include "builtin_class_registry.h"
#include "object.h"
#include "value.h"
#include <cstdint>

namespace cl
{
    class VirtualMachine;

    class Instance : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::Instance;

        explicit Instance(ClassObject *cls);

        static size_t size_for(uint32_t dynamic_inline_slot_count)
        {
            return sizeof(Instance) + sizeof(Value) * dynamic_inline_slot_count;
        }

        static DynamicLayoutSpec layout_spec_for(ClassObject *cls);

    public:
        static constexpr bool has_dynamic_layout = true;
        static constexpr uint32_t static_value_offset_in_words()
        {
            return Object::static_value_offset_in_words();
        }
    };

    BuiltinClassDefinition make_instance_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_INSTANCE_H
