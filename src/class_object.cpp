#include "class_object.h"
#include "attr.h"
#include "constructor_thunk.h"
#include "function.h"
#include "runtime_helpers.h"
#include "str.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <vector>

namespace cl
{
    static std::deque<ClassObject *> class_deque_from_tuple(const Tuple *tuple)
    {
        std::vector<ClassObject *> vector =
            vector_from_tuple<ClassObject>(tuple);
        return std::deque<ClassObject *>(vector.begin(), vector.end());
    }

    static bool
    appears_in_any_tail(ClassObject *candidate,
                        const std::vector<std::deque<ClassObject *>> &sequences)
    {
        for(const std::deque<ClassObject *> &sequence: sequences)
        {
            if(sequence.size() <= 1)
            {
                continue;
            }
            if(std::find(sequence.begin() + 1, sequence.end(), candidate) !=
               sequence.end())
            {
                return true;
            }
        }
        return false;
    }

    static ClassObject *find_c3_merge_candidate(
        const std::vector<std::deque<ClassObject *>> &sequences,
        bool &has_remaining)
    {
        has_remaining = false;
        for(const std::deque<ClassObject *> &sequence: sequences)
        {
            if(sequence.empty())
            {
                continue;
            }
            has_remaining = true;

            ClassObject *candidate = sequence.front();
            if(!appears_in_any_tail(candidate, sequences))
            {
                return candidate;
            }
        }
        return nullptr;
    }

    static void
    remove_c3_merge_candidate(ClassObject *candidate,
                              std::vector<std::deque<ClassObject *>> &sequences)
    {
        for(std::deque<ClassObject *> &sequence: sequences)
        {
            while(!sequence.empty() && sequence.front() == candidate)
            {
                sequence.pop_front();
            }
        }
    }

    static Value compute_mro(ClassObject *cls, const Tuple *bases)
    {
        TValue<String> dunder_mro_name = interned_string(L"__mro__");
        std::vector<std::deque<ClassObject *>> sequences;
        sequences.reserve(bases->size() + 1);
        for(size_t base_idx = 0; base_idx < bases->size(); ++base_idx)
        {
            ClassObject *base =
                assume_convert_to<ClassObject>(bases->item_unchecked(base_idx));
            Value base_mro_value = base->get_own_property(dunder_mro_name);
            sequences.push_back(class_deque_from_tuple(
                assume_convert_to<Tuple>(base_mro_value)));
        }
        sequences.push_back(class_deque_from_tuple(bases));

        std::vector<ClassObject *> linearized;
        linearized.push_back(cls);

        while(true)
        {
            bool has_remaining = false;
            ClassObject *candidate =
                find_c3_merge_candidate(sequences, has_remaining);
            if(!has_remaining)
            {
                break;
            }
            if(candidate == nullptr)
            {
                throw std::runtime_error(
                    "TypeError: cannot create a consistent method resolution "
                    "order");
            }

            linearized.push_back(candidate);
            remove_c3_merge_candidate(candidate, sequences);
        }

        return tuple_from_vector<ClassObject>(linearized);
    }

    void ClassObject::validate_bases(TValue<Tuple> bases)
    {
        Tuple *bases_tuple = bases.extract();
        std::vector<ClassObject *> seen_bases;
        seen_bases.reserve(bases_tuple->size());
        for(size_t idx = 0; idx < bases_tuple->size(); ++idx)
        {
            ClassObject *base =
                try_convert_to<ClassObject>(bases_tuple->item_unchecked(idx));
            if(base == nullptr)
            {
                throw std::runtime_error(
                    "TypeError: class bases must be class objects");
            }
            if(std::find(seen_bases.begin(), seen_bases.end(), base) !=
               seen_bases.end())
            {
                throw std::runtime_error("TypeError: duplicate base class");
            }
            seen_bases.push_back(base);
        }
    }

    ClassObject::ClassObject(BootstrapObjectTag, TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             ClassObject *single_base,
                             ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : Object(BootstrapObjectTag{}, native_layout_id, compact_layout()),
          name(_name), bases(Value::not_present()), mro(Value::not_present()),
          mro_shape_and_contents_validity_cell(nullptr),
          mro_shape_and_metaclass_mro_shape_and_contents_validity_cell(nullptr),
          attached_mro_shape_validity_cells(),
          attached_mro_shape_and_contents_validity_cells(),
          instance_root_shape(nullptr), constructor_thunk(nullptr),
          instance_default_inline_slot_count(
              _instance_default_inline_slot_count)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        DescriptorFlags instance_class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly);
        instance_class_flags |= descriptor_flag(DescriptorFlag::StableSlot);
        instance_root_shape = Shape::make_root_with_single_descriptor(
            Value::from_oop(this), dunder_class_name,
            DescriptorInfo::make(StorageLocation{0, StorageKind::Inline},
                                 instance_class_flags),
            1, instance_shape_flags);

        TValue<String> dunder_name_name = interned_string(L"__name__");
        TValue<String> dunder_bases_name = interned_string(L"__bases__");
        TValue<String> dunder_mro_name = interned_string(L"__mro__");
        TValue<String> dunder_new_name = interned_string(L"__new__");
        TValue<String> dunder_init_name = interned_string(L"__init__");
        DescriptorFlags class_metadata_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        DescriptorFlags class_predefined_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[kClassPredefinedSlotCount] = {
            ShapeRootDescriptor{
                dunder_class_name,
                DescriptorInfo::make(StorageLocation{kClassMetadataSlotClass,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_name_name,
                DescriptorInfo::make(StorageLocation{kClassMetadataSlotName,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_bases_name,
                DescriptorInfo::make(StorageLocation{kClassMetadataSlotBases,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_mro_name,
                DescriptorInfo::make(
                    StorageLocation{kClassMetadataSlotMro, StorageKind::Inline},
                    class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_new_name,
                DescriptorInfo::make(StorageLocation{kClassPredefinedSlotNew,
                                                     StorageKind::Inline},
                                     class_predefined_flags)},
            ShapeRootDescriptor{
                dunder_init_name,
                DescriptorInfo::make(StorageLocation{kClassPredefinedSlotInit,
                                                     StorageKind::Inline},
                                     class_predefined_flags)},
        };
        set_shape(Shape::make_root_with_descriptors(
            Value::from_oop(this), descriptors, kClassPredefinedSlotCount,
            kClassPredefinedSlotCount, kClassMetadataSlotCount,
            class_shape_flags));

        for(uint32_t slot_idx = 0;
            slot_idx < kClassExtraInlineAttributeSlotCount; ++slot_idx)
        {
            class_extra_inline_attribute_slots[slot_idx] = Value::not_present();
        }
        if(single_base == nullptr)
        {
            bases = Value::None();
            mro = Value::None();
        }
        else
        {
            Value bases_tuple = make_bases_tuple(single_base);
            bases = bases_tuple;
            mro = compute_mro(this, assume_convert_to<Tuple>(bases_tuple));
        }
    }

    ClassObject::ClassObject(ClassObject *metaclass, TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             ClassObject *single_base,
                             ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : ClassObject(BootstrapObjectTag{}, _name,
                      _instance_default_inline_slot_count, single_base,
                      class_shape_flags, instance_shape_flags)
    {
        install_bootstrap_class(metaclass);
    }

    ClassObject::ClassObject(ClassObject *metaclass, TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             TValue<Tuple> _bases, ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : ClassObject(BootstrapObjectTag{}, _name,
                      _instance_default_inline_slot_count, nullptr,
                      class_shape_flags, instance_shape_flags)
    {
        install_bootstrap_class(metaclass);
        validate_bases(_bases);
        bases = _bases;
        mro = compute_mro(this, _bases.extract());
    }

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             ClassObject *single_base,
                             ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : ClassObject(active_vm()->type_class(), _name,
                      _instance_default_inline_slot_count, single_base,
                      class_shape_flags, instance_shape_flags)
    {
    }

    ClassObject *ClassObject::make_bootstrap_builtin_class(
        TValue<String> name, uint32_t instance_default_inline_slot_count,
        const BuiltinClassMethod *methods, uint32_t method_count)
    {
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType);
        ClassObject *type = active_vm()->type_class();
        assert(type != nullptr);
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            BootstrapObjectTag{}, name, instance_default_inline_slot_count,
            nullptr, class_shape_flags, fixed_attribute_shape_flags());
        cls->install_bootstrap_class(type);

        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        for(uint32_t method_idx = 0; method_idx < method_count; ++method_idx)
        {
            bool stored = cls->define_own_property(methods[method_idx].name,
                                                   methods[method_idx].value,
                                                   method_flags);
            assert(stored);
            (void)stored;
        }

        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags | fixed_attribute_shape_flags()));
        return cls;
    }

    ClassObject *ClassObject::make_builtin_class(
        TValue<String> name, uint32_t instance_default_inline_slot_count,
        const BuiltinClassMethod *methods, uint32_t method_count,
        ClassObject *single_base)
    {
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType);
        ClassObject *type = active_vm()->type_class();
        assert(type != nullptr);
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            type, name, instance_default_inline_slot_count, single_base,
            class_shape_flags, fixed_attribute_shape_flags());

        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        for(uint32_t method_idx = 0; method_idx < method_count; ++method_idx)
        {
            bool stored = cls->define_own_property(methods[method_idx].name,
                                                   methods[method_idx].value,
                                                   method_flags);
            assert(stored);
            (void)stored;
        }

        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags | fixed_attribute_shape_flags()));
        return cls;
    }

    void ClassObject::validate_inline_slot_layout()
    {
        static_assert(sizeof(MemberTValue<String>) == sizeof(Value));
        static_assert(sizeof(MemberValue) == sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, cls) ==
                      ClassObject::static_value_offset_in_words() *
                          sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, name) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          kClassMetadataSlotName * sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, bases) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          kClassMetadataSlotBases * sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, mro) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          kClassMetadataSlotMro * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, class_extra_inline_attribute_slots) ==
            CL_OFFSETOF(ClassObject, cls) +
                kClassMetadataSlotCount * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, mro_shape_and_contents_validity_cell) ==
            CL_OFFSETOF(ClassObject, cls) +
                kClassInlineStorageSlotCount * sizeof(Value));
        static_assert(
            CL_OFFSETOF(
                ClassObject,
                mro_shape_and_metaclass_mro_shape_and_contents_validity_cell) ==
            CL_OFFSETOF(ClassObject, cls) +
                (kClassInlineStorageSlotCount + 1) * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, attached_mro_shape_validity_cells) ==
            CL_OFFSETOF(ClassObject, cls) +
                (kClassInlineStorageSlotCount + 2) * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject,
                        attached_mro_shape_and_contents_validity_cells) ==
            CL_OFFSETOF(ClassObject, cls) +
                (kClassInlineStorageSlotCount + 2 +
                 HeapPtrArray<ValidityCell>::embedded_value_count) *
                    sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, instance_root_shape) ==
            CL_OFFSETOF(ClassObject, cls) +
                (kClassInlineStorageSlotCount + 2 +
                 2 * HeapPtrArray<ValidityCell>::embedded_value_count) *
                    sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, constructor_thunk) ==
            CL_OFFSETOF(ClassObject, cls) +
                (kClassInlineStorageSlotCount + 2 +
                 2 * HeapPtrArray<ValidityCell>::embedded_value_count + 1) *
                    sizeof(Value));
    }

    BuiltinClassDefinition make_type_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ClassObject};
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType) |
                                       fixed_attribute_shape_flags();
        ClassObject::validate_inline_slot_layout();
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            BootstrapObjectTag{},
            vm->get_or_create_interned_string_value(L"type"),
            ClassObject::kClassInlineStorageSlotCount, nullptr,
            class_shape_flags, fixed_attribute_shape_flags());
        cls->install_bootstrap_class(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

    Shape *ClassObject::get_instance_root_shape() const
    {
        return instance_root_shape.extract();
    }

    void ClassObject::install_bootstrap_inheritance(Value bases_tuple,
                                                    Value mro_tuple)
    {
        assert(can_convert_to<Tuple>(bases_tuple));
        assert(can_convert_to<Tuple>(mro_tuple));
        bases = bases_tuple;
        mro = mro_tuple;
        invalidate_lookup_validity_cells_for_shape_change();
    }

    ValidityCell *
    ClassObject::create_mro_shape_and_contents_validity_cell_slow() const
    {
        ValidityCell *cell = make_internal_raw<ValidityCell>();
        mro_shape_and_contents_validity_cell = cell;
        install_validity_cell_along_mro(
            cell, MroValidityCellInstallMode::SkipSelf,
            MroValidityCellDependency::ShapeAndContents);

        return cell;
    }

    ValidityCell *ClassObject::
        create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_slow()
            const
    {
        ValidityCell *cell = make_internal_raw<ValidityCell>();
        mro_shape_and_metaclass_mro_shape_and_contents_validity_cell = cell;
        install_validity_cell_along_mro(cell,
                                        MroValidityCellInstallMode::SkipSelf,
                                        MroValidityCellDependency::ShapeOnly);

        ClassObject *metaclass = get_class().extract();
        if(metaclass != this)
        {
            metaclass->install_validity_cell_along_mro(
                cell, MroValidityCellInstallMode::IncludeSelf,
                MroValidityCellDependency::ShapeAndContents);
        }

        return cell;
    }

    void ClassObject::install_validity_cell_along_mro(
        ValidityCell *cell, MroValidityCellInstallMode mode,
        MroValidityCellDependency dependency) const
    {
        assert(cell != nullptr);
        assert(cell->is_valid());

        Value mro_value = inline_slot_base()[kClassMetadataSlotMro];
        Tuple *mro = assume_convert_to<Tuple>(mro_value);
        uint32_t start_idx =
            mode == MroValidityCellInstallMode::IncludeSelf ? 0 : 1;
        for(uint32_t mro_idx = start_idx; mro_idx < mro->size(); ++mro_idx)
        {
            ClassObject *base =
                assume_convert_to<ClassObject>(mro->item_unchecked(mro_idx));
            base->attach_mro_validity_cell(cell, dependency);
        }
    }

    void ClassObject::attach_mro_validity_cell(
        ValidityCell *cell, MroValidityCellDependency dependency) const
    {
        assert(cell != nullptr);
        assert(cell->is_valid());

        HeapPtrArray<ValidityCell> &attached_cells =
            dependency == MroValidityCellDependency::ShapeOnly
                ? attached_mro_shape_validity_cells
                : attached_mro_shape_and_contents_validity_cells;

        for(size_t idx = 0; idx < attached_cells.size(); ++idx)
        {
            ValidityCell *attached_cell = attached_cells[idx];
            if(!attached_cell->is_valid())
            {
                attached_cells.set(idx, cell);
                return;
            }
        }

        attached_cells.push_back(cell);
    }

    static void invalidate_attached_cells(HeapPtrArray<ValidityCell> &cells)
    {
        for(ValidityCell *cell: cells)
        {
            cell->invalidate();
        }
        cells.clear();
    }

    void ClassObject::invalidate_lookup_validity_cells_for_contents_change()
    {
        constructor_thunk = nullptr;
        invalidate_attached_cells(
            attached_mro_shape_and_contents_validity_cells);
        if(mro_shape_and_contents_validity_cell != nullptr)
        {
            mro_shape_and_contents_validity_cell->invalidate();
            mro_shape_and_contents_validity_cell = nullptr;
        }
    }

    void ClassObject::invalidate_lookup_validity_cells_for_shape_change()
    {
        invalidate_attached_cells(attached_mro_shape_validity_cells);
        invalidate_lookup_validity_cells_for_contents_change();

        if(mro_shape_and_metaclass_mro_shape_and_contents_validity_cell !=
           nullptr)
        {
            mro_shape_and_metaclass_mro_shape_and_contents_validity_cell
                ->invalidate();
            mro_shape_and_metaclass_mro_shape_and_contents_validity_cell =
                nullptr;
        }
    }

    ConstructorThunkLookup ClassObject::create_constructor_thunk_slow() const
    {
        ClassObject *self = const_cast<ClassObject *>(this);
        if(get_class().extract() != active_vm()->type_class())
        {
            return ConstructorThunkLookup{nullptr, nullptr};
        }

        ValidityCell *lookup_cell =
            get_or_create_mro_shape_and_contents_validity_cell();
        Function *existing = constructor_thunk.extract();
        if(existing != nullptr && lookup_cell->is_valid())
        {
            return ConstructorThunkLookup{existing, lookup_cell};
        }

        TValue<String> new_name(interned_string(L"__new__"));
        AttributeReadDescriptor new_descriptor =
            resolve_attr_read_descriptor(Value::from_oop(self), new_name);
        if(new_descriptor.is_found())
        {
            return ConstructorThunkLookup{nullptr, nullptr};
        }

        TValue<String> init_name(interned_string(L"__init__"));
        AttributeReadDescriptor init_descriptor =
            resolve_attr_read_descriptor(Value::from_oop(self), init_name);
        if(!init_descriptor.is_found())
        {
            TValue<Function> thunk =
                make_constructor_thunk_function(self, Value::not_present());
            constructor_thunk = thunk.extract();
            return ConstructorThunkLookup{thunk.extract(), lookup_cell};
        }

        if(!init_descriptor.is_cacheable() ||
           init_descriptor.plan.kind ==
               AttributeReadPlanKind::DataDescriptorGet ||
           init_descriptor.plan.kind ==
               AttributeReadPlanKind::NonDataDescriptorGet)
        {
            return ConstructorThunkLookup{nullptr, nullptr};
        }

        Value init_value =
            load_attr_from_plan(Value::from_oop(self), init_descriptor.plan);
        if(!can_convert_to<Function>(init_value))
        {
            return ConstructorThunkLookup{nullptr, nullptr};
        }

        TValue<Function> thunk =
            make_constructor_thunk_function(self, init_value);
        constructor_thunk = thunk.extract();
        return ConstructorThunkLookup{thunk.extract(), lookup_cell};
    }

    Value ClassObject::make_bases_tuple(ClassObject *single_base) const
    {
        assert(single_base != nullptr);
        Tuple *bases = make_object_raw<Tuple>(1);
        bases->initialize_item_unchecked(0, Value::from_oop(single_base));
        return Value::from_oop(bases);
    }

}  // namespace cl
