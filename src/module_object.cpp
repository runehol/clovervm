#include "module_object.h"
#include "attribute_descriptor.h"
#include "class_object.h"
#include "runtime_helpers.h"
#include "shape.h"
#include "str.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    static DescriptorFlags module_builtins_descriptor_flags()
    {
        return descriptor_flag(DescriptorFlag::ReadOnly) |
               descriptor_flag(DescriptorFlag::StableSlot);
    }

    ModuleObject::ModuleObject(ClassObject *cls, TValue<String> _name,
                               Value _builtins)
        : SlotObject(cls, native_layout), name_binding(_name.raw_value()),
          builtins_binding(_builtins)
    {
        if(!_builtins.is_not_present())
        {
            TValue<String> dunder_builtins_name =
                interned_string(L"__builtins__");
            set_shape(get_shape()->derive_transition(
                dunder_builtins_name, ShapeTransitionVerb::Add,
                module_builtins_descriptor_flags()));
        }
        for(uint32_t slot_idx = 0;
            slot_idx < module_extra_inline_attribute_slot_count; ++slot_idx)
        {
            module_extra_inline_attribute_slots[slot_idx] =
                Value::not_present();
        }
    }

    void ModuleObject::set_name_binding(Value value)
    {
        value.assert_not_vm_sentinel();
        name_binding = value;
    }

    void ModuleObject::set_builtins_binding(Value value)
    {
        value.assert_not_vm_sentinel();
        builtins_binding = value;
    }

    static void install_module_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> dunder_name_name = interned_string(L"__name__");
        TValue<String> dunder_builtins_name = interned_string(L"__builtins__");

        DescriptorFlags class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot) |
            descriptor_flag(DescriptorFlag::ShapeClassValue);
        DescriptorFlags name_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[] = {
            ShapeRootDescriptor{dunder_class_name,
                                DescriptorInfo::make(
                                    StorageLocation::not_found(), class_flags)},
            ShapeRootDescriptor{
                dunder_name_name,
                DescriptorInfo::make(
                    StorageLocation{ModuleObject::module_predefined_slot_name,
                                    StorageKind::Inline},
                    name_flags)},
            ShapeRootDescriptor{
                dunder_builtins_name,
                DescriptorInfo::make(
                    StorageLocation{
                        ModuleObject::module_predefined_slot_builtins,
                        StorageKind::Inline},
                    module_builtins_descriptor_flags())},
        };
        cls->install_builtin_instance_root_shape(
            descriptors, std::size(descriptors),
            ModuleObject::module_predefined_slot_count,
            ModuleObject::module_predefined_slot_count,
            mutable_attribute_shape_flags());
    }

    BuiltinClassDefinition make_module_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ModuleObject};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"module"),
            ModuleObject::module_inline_storage_slot_count, nullptr, 0,
            vm->object_class());
        install_module_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

}  // namespace cl
