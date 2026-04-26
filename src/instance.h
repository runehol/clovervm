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

        Instance(HeapLayout layout, ClassObject *cls);

        static size_t size_for(uint32_t dynamic_inline_slot_count)
        {
            return sizeof(Instance) + sizeof(Value) * dynamic_inline_slot_count;
        }

        static DynamicLayoutSpec layout_spec_for(ClassObject *cls);

    public:
        CL_DECLARE_DYNAMIC_LAYOUT_EXTENDS_WITH_VALUES(Instance, Object, 0);
    };

    BuiltinClassDefinition make_instance_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_INSTANCE_H
