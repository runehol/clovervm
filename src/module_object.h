#ifndef CL_MODULE_OBJECT_H
#define CL_MODULE_OBJECT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned.h"
#include "scope.h"
#include "typed_value.h"
#include "validity_cell.h"
#include "value.h"
#include "vm_array.h"
#include <cstdint>
#include <type_traits>

namespace cl
{
    class ClassObject;
    class ModuleObject;
    class String;
    class VirtualMachine;

    class ModuleBuiltinsLookup
    {
    public:
        Value builtins_object;
        ModuleObject *builtins_module;
        ValidityCell *lookup_validity_cell;

        static ModuleBuiltinsLookup module(ModuleObject *builtins_module,
                                           ValidityCell *lookup_validity_cell);

        static ModuleBuiltinsLookup uncacheable(Value builtins_object);

        bool is_module() const { return builtins_module != nullptr; }
    };

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
        void delete_builtins_binding();
        ValidityCell *current_module_globals_validity_cell() const
        {
            return module_globals_validity_cell.extract();
        }
        ValidityCell *current_module_builtins_validity_cell() const
        {
            return module_builtins_validity_cell.extract();
        }
        ALWAYSINLINE ValidityCell *
        get_or_create_module_globals_validity_cell() const
        {
            ValidityCell *cell = module_globals_validity_cell.extract();
            if(likely(cell != nullptr && cell->is_valid()))
            {
                return cell;
            }
            return create_module_globals_validity_cell_slow();
        }
        ModuleBuiltinsLookup get_module_builtins_lookup() const;
        uint32_t attached_module_builtins_validity_cell_count() const
        {
            return attached_module_builtins_validity_cells.size();
        }
        void attach_module_builtins_validity_cell(ValidityCell *cell) const;
        void invalidate_module_lookup_validity_cells();
        Scope *legacy_module_scope() const
        {
            return legacy_module_scope_.extract();
        }
        void set_legacy_module_scope(Scope *scope)
        {
            legacy_module_scope_ = scope;
        }

    private:
        static constexpr uint32_t module_extra_inline_attribute_slot_count =
            module_inline_storage_slot_count - module_predefined_slot_count;

        NOINLINE ValidityCell *create_module_globals_validity_cell_slow() const;
        NOINLINE ValidityCell *
        create_module_builtins_validity_cell_slow() const;

        Member<Value> name_binding;
        Member<Value> builtins_binding;
        Value module_extra_inline_attribute_slots
            [module_extra_inline_attribute_slot_count];
        mutable MemberHeapPtr<ValidityCell> module_globals_validity_cell;
        mutable MemberHeapPtr<ValidityCell> module_builtins_validity_cell;
        mutable HeapPtrArray<ValidityCell>
            attached_module_builtins_validity_cells;
        MemberHeapPtr<Scope> legacy_module_scope_;

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            ModuleObject, SlotObject,
            module_inline_storage_slot_count + 3 +
                HeapPtrArray<ValidityCell>::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(ModuleObject);
    };

    static_assert(std::is_trivially_destructible_v<ModuleObject>);

    BuiltinClassDefinition make_module_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_MODULE_OBJECT_H
