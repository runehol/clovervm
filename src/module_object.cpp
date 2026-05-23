#include "module_object.h"
#include "attribute_descriptor.h"
#include "class_object.h"
#include "runtime_helpers.h"
#include "shape.h"
#include "str.h"
#include "virtual_machine.h"
#include <algorithm>
#include <iterator>

namespace cl
{
    static constexpr size_t kMaxAttachedValidityCellReuseScan = 8;

    static DescriptorFlags module_builtins_descriptor_flags()
    {
        return descriptor_flag(DescriptorFlag::ReadOnly) |
               descriptor_flag(DescriptorFlag::StableSlot);
    }

    static void ensure_module_builtins_descriptor_present(ModuleObject *module)
    {
        TValue<String> dunder_builtins_name = interned_string(L"__builtins__");
        if(module->get_shape()
               ->resolve_present_property(dunder_builtins_name)
               .is_found())
        {
            return;
        }

        module->set_shape(module->get_shape()->derive_transition(
            dunder_builtins_name, ShapeTransitionVerb::Add,
            module_builtins_descriptor_flags()));
    }

    static void ensure_module_builtins_descriptor_absent(ModuleObject *module)
    {
        TValue<String> dunder_builtins_name = interned_string(L"__builtins__");
        if(!module->get_shape()
                ->resolve_present_property(dunder_builtins_name)
                .is_found())
        {
            return;
        }

        module->set_shape(module->get_shape()->derive_transition(
            dunder_builtins_name, ShapeTransitionVerb::Delete));
    }

    ModuleBuiltinsLookup
    ModuleBuiltinsLookup::module(ModuleObject *builtins_module,
                                 ValidityCell *lookup_validity_cell)
    {
        assert(builtins_module != nullptr);
        assert(lookup_validity_cell != nullptr);
        return ModuleBuiltinsLookup{Value::from_oop(builtins_module),
                                    builtins_module, lookup_validity_cell};
    }

    ModuleBuiltinsLookup
    ModuleBuiltinsLookup::uncacheable(Value builtins_object)
    {
        return ModuleBuiltinsLookup{builtins_object, nullptr, nullptr};
    }

    ModuleObject::ModuleObject(ClassObject *cls, TValue<String> _name,
                               Value _builtins)
        : SlotObject(cls, native_layout), name_binding(_name.raw_value()),
          builtins_binding(_builtins), module_globals_validity_cell(nullptr),
          module_builtins_validity_cell(nullptr),
          attached_dependent_lookup_validity_cells()
    {
        if(!_builtins.is_not_present())
        {
            ensure_module_builtins_descriptor_present(this);
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
        ensure_module_builtins_descriptor_present(this);
        builtins_binding = value;
        invalidate_module_builtins_binding_validity_cell();
    }

    void ModuleObject::delete_builtins_binding()
    {
        builtins_binding = Value::not_present();
        ensure_module_builtins_descriptor_absent(this);
        invalidate_module_builtins_binding_validity_cell();
    }

    ModuleBuiltinsLookup ModuleObject::get_module_builtins_lookup() const
    {
        Value builtins = get_builtins_binding();
        if(builtins.is_not_present())
        {
            builtins = active_vm()->global_builtins_module().raw_value();
        }
        if(!can_convert_to<ModuleObject>(builtins))
        {
            return ModuleBuiltinsLookup::uncacheable(builtins);
        }

        ModuleObject *builtins_module =
            assume_convert_to<ModuleObject>(builtins);
        ValidityCell *cell = module_builtins_validity_cell.extract();
        if(likely(cell != nullptr && cell->is_valid()))
        {
            return ModuleBuiltinsLookup::module(builtins_module, cell);
        }

        cell = create_module_builtins_validity_cell_slow();
        builtins_module->attach_dependent_lookup_validity_cell(cell);
        return ModuleBuiltinsLookup::module(builtins_module, cell);
    }

    ValidityCell *ModuleObject::create_module_globals_validity_cell_slow() const
    {
        ValidityCell *cell = make_internal_raw<ValidityCell>();
        module_globals_validity_cell = cell;
        return cell;
    }

    ValidityCell *
    ModuleObject::create_module_builtins_validity_cell_slow() const
    {
        ValidityCell *cell = make_internal_raw<ValidityCell>();
        module_builtins_validity_cell = cell;
        return cell;
    }

    void ModuleObject::attach_dependent_lookup_validity_cell(
        ValidityCell *cell) const
    {
        assert(cell != nullptr);
        assert(cell->is_valid());

        size_t reuse_scan_count =
            std::min(attached_dependent_lookup_validity_cells.size(),
                     kMaxAttachedValidityCellReuseScan);
        for(size_t idx = 0; idx < reuse_scan_count; ++idx)
        {
            ValidityCell *attached_cell =
                attached_dependent_lookup_validity_cells[idx];
            if(!attached_cell->is_valid())
            {
                attached_dependent_lookup_validity_cells.set(idx, cell);
                return;
            }
        }

        attached_dependent_lookup_validity_cells.push_back(cell);
    }

    static void invalidate_attached_cells(HeapPtrArray<ValidityCell> &cells)
    {
        for(ValidityCell *cell: cells)
        {
            cell->invalidate();
        }
        cells.clear();
    }

    void ModuleObject::invalidate_module_builtins_binding_validity_cell()
    {
        if(module_builtins_validity_cell != nullptr)
        {
            module_builtins_validity_cell->invalidate();
            module_builtins_validity_cell = nullptr;
        }
    }

    void ModuleObject::invalidate_module_lookup_validity_cells()
    {
        invalidate_attached_cells(attached_dependent_lookup_validity_cells);
        if(module_globals_validity_cell != nullptr)
        {
            module_globals_validity_cell->invalidate();
            module_globals_validity_cell = nullptr;
        }
        invalidate_module_builtins_binding_validity_cell();
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
            shape_flag(ShapeFlag::IsModuleObject));
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
