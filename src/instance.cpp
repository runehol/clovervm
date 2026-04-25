#include "instance.h"
#include "class_object.h"
#include "runtime_helpers.h"
#include "virtual_machine.h"

namespace cl
{

    Instance::Instance(Value _cls, Shape *_shape)
        : Object(_cls.get_ptr<ClassObject>(), native_layout_id)
    {
        set_shape(_shape);
        uint32_t factory_default_inline_slot_count =
            get_shape()->get_factory_default_inline_slot_count();
        for(uint32_t slot_idx = 1; slot_idx < factory_default_inline_slot_count;
            ++slot_idx)
        {
            inline_slot_base()[slot_idx] = Value::not_present();
        }

        TValue<String> dunder_class_name = interned_string(L"__class__");
        StorageLocation class_location =
            get_shape()->resolve_present_property(dunder_class_name);
        assert(class_location.is_found());
        assert(class_location.kind == StorageKind::Inline);
        assert(class_location.physical_idx == 0);
    }

    BuiltinClassDefinition make_instance_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Instance};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"object"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    DynamicLayoutSpec Instance::layout_spec_for(Value cls, Shape *shape)
    {
        uint32_t factory_default_inline_slot_count =
            shape->get_factory_default_inline_slot_count();
        assert(factory_default_inline_slot_count >= 1);
        uint32_t dynamic_inline_slot_count =
            factory_default_inline_slot_count - 1;
        return DynamicLayoutSpec{
            round_up_to_16byte_units(size_for(dynamic_inline_slot_count)),
            factory_default_inline_slot_count};
    }

}  // namespace cl
