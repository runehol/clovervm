#ifndef CL_MODULE_OBJECT_H
#define CL_MODULE_OBJECT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned.h"
#include "typed_value.h"
#include "value.h"
#include <cstdint>
#include <type_traits>

namespace cl
{
    class ClassObject;
    class String;
    class VirtualMachine;

    class ModuleObject : public SlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ModuleObject;
        static constexpr uint32_t module_predefined_slot_name = 0;
        static constexpr uint32_t module_predefined_slot_builtins = 1;
        static constexpr uint32_t module_predefined_slot_count = 2;
        static constexpr uint32_t module_inline_storage_slot_count = 256;

        ModuleObject(ClassObject *cls, TValue<String> name,
                     Value builtins = Value::not_present());

        Value get_name_binding() const { return name_binding.value(); }
        void set_name_binding(Value value);
        Value get_builtins_binding() const { return builtins_binding.value(); }
        void set_builtins_binding(Value value);
        void delete_builtins_binding()
        {
            builtins_binding = Value::not_present();
        }

    private:
        static constexpr uint32_t module_extra_inline_attribute_slot_count =
            module_inline_storage_slot_count - module_predefined_slot_count;

        Member<Value> name_binding;
        Member<Value> builtins_binding;
        Value module_extra_inline_attribute_slots
            [module_extra_inline_attribute_slot_count];

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ModuleObject, SlotObject,
                                             module_inline_storage_slot_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(ModuleObject);
    };

    static_assert(std::is_trivially_destructible_v<ModuleObject>);

    BuiltinClassDefinition make_module_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_MODULE_OBJECT_H
