#include "module_object.h"
#include "attribute_descriptor.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "native_function.h"
#include "runtime_helpers.h"
#include "shape.h"
#include "str.h"
#include "string_builder.h"
#include "virtual_machine.h"
#include <algorithm>
#include <iterator>

namespace cl
{
    static constexpr size_t kMaxAttachedValidityCellReuseScan = 8;

    static DescriptorFlags module_builtins_descriptor_flags()
    {
        return descriptor_flag(DescriptorFlag::StableSlot) |
               descriptor_flag(DescriptorFlag::SpecialMutate);
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

    static void set_module_attr(ModuleObject *module, const wchar_t *name,
                                Value value)
    {
        bool stored = module->set_own_property(interned_string(name), value);
        assert(stored);
        (void)stored;
    }

    static Value native_module_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<ModuleObject>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"module.__repr__ expects a module receiver");
        }

        ModuleObject *module = self.get_ptr<ModuleObject>();
        Value name = module->get_name_binding();
        StringBuilder builder;
        builder.append_c_str(L"<module '");
        if(can_convert_to<String>(name))
        {
            builder.append_string(TValue<String>::from_value_assumed(name));
        }
        else
        {
            builder.append_c_str(L"<unknown>");
        }
        builder.append_char(L'\'');

        Value file = module->get_own_property(interned_string(L"__file__"));
        if(can_convert_to<String>(file))
        {
            builder.append_c_str(L" from '");
            builder.append_string(TValue<String>::from_value_assumed(file));
            builder.append_c_str(L"'>");
            return builder.finish();
        }

        Value path = module->get_own_property(interned_string(L"__path__"));
        if(!path.is_not_present())
        {
            builder.append_c_str(L" (namespace) from ");
            CL_PROPAGATE_EXCEPTION(builder.append_repr(path));
            builder.append_char(L'>');
            return builder.finish();
        }

        builder.append_c_str(L" (built-in)>");
        return builder.finish();
    }

    ModuleObject::ModuleObject(ClassObject *cls, TValue<String> _name,
                               Value _builtins, Value doc, Value package,
                               Value loader, Value spec, Value file)
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
        set_module_attr(this, L"__doc__", doc);
        set_module_attr(this, L"__package__", package);
        set_module_attr(this, L"__loader__", loader);
        set_module_attr(this, L"__spec__", spec);
        if(!file.is_not_present())
        {
            set_module_attr(this, L"__file__", file);
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
        TValue<String> dunder_name_name = interned_string(L"__name__");
        TValue<String> dunder_builtins_name = interned_string(L"__builtins__");
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            ModuleObject::module_predefined_slot_count)
            .add_slot(dunder_name_name,
                      ModuleObject::module_predefined_slot_name)
            .reserve_slot(ModuleObject::module_predefined_slot_builtins)
            .add_descriptor(
                dunder_builtins_name,
                DescriptorInfo::make(
                    StorageLocation{
                        ModuleObject::module_predefined_slot_builtins,
                        StorageKind::Inline},
                    module_builtins_descriptor_flags(),
                    DescriptorSpecialKind::ModuleBuiltins))
            .install(shape_flag(ShapeFlag::IsModuleObject));
    }

    BuiltinClassDefinition make_module_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ModuleObject};
        ClassObject *cls = ClassObject::make_builtin_class<ModuleObject>(
            vm->get_or_create_interned_string_value(L"module"),
            ModuleObject::module_inline_storage_slot_count, nullptr, 0,
            vm->object_class());
        install_module_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

    void install_module_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__repr__", native_module_repr,
                                     L"Return repr(self)."),
        };
        install_builtin_intrinsic_methods(vm, vm->module_class(), methods,
                                          std::size(methods));
    }

}  // namespace cl
