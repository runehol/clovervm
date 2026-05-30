#include "class_object.h"
#include "attr.h"
#include "constructor_thunk.h"
#include "function.h"
#include "runtime_helpers.h"
#include "str.h"
#include "thread_state.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <algorithm>
#include <deque>
#include <iterator>
#include <vector>

namespace cl
{
    static constexpr size_t kMaxAttachedValidityCellReuseScan = 8;

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

    bool is_subclass_of(ClassObject *cls, ClassObject *base)
    {
        if(cls == base)
        {
            return true;
        }

        Value mro_value = cls->get_mro_value();
        if(!can_convert_to<Tuple>(mro_value))
        {
            return false;
        }

        Tuple *mro = mro_value.get_ptr<Tuple>();
        for(size_t idx = 0; idx < mro->size(); ++idx)
        {
            if(mro->item_unchecked(idx) == Value::from_oop(base))
            {
                return true;
            }
        }
        return false;
    }

    bool is_instance_of(Object *obj, ClassObject *cls)
    {
        return is_subclass_of(obj->get_shape()->get_class(), cls);
    }

    static Expected<TValue<Tuple>> compute_mro(ClassObject *cls,
                                               const Tuple *bases)
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
                return Expected<TValue<Tuple>>::raise_exception(
                    L"TypeError",
                    L"cannot create a consistent method resolution order");
            }

            linearized.push_back(candidate);
            remove_c3_merge_candidate(candidate, sequences);
        }

        return Expected<TValue<Tuple>>::ok(TValue<Tuple>::from_value_assumed(
            tuple_from_vector<ClassObject>(linearized)));
    }

    Expected<void> ClassObject::validate_bases(TValue<Tuple> bases)
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
                return Expected<void>::raise_exception(
                    L"TypeError", L"class bases must be class objects");
            }
            if(std::find(seen_bases.begin(), seen_bases.end(), base) !=
               seen_bases.end())
            {
                return Expected<void>::raise_exception(L"TypeError",
                                                       L"duplicate base class");
            }
            seen_bases.push_back(base);
        }
        return Expected<void>::ok();
    }

    ClassObject::ClassObject(BootstrapObjectTag, TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             ClassObject *single_base,
                             NativeLayoutId _instance_native_layout_id,
                             ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : SlotObject(BootstrapObjectTag{}, native_layout), name(_name),
          bases(Value::not_present()), mro(Value::not_present()),
          mro_shape_and_contents_validity_cell(nullptr),
          mro_shape_and_metaclass_mro_shape_and_contents_validity_cell(nullptr),
          attached_mro_shape_validity_cells(),
          attached_mro_shape_and_contents_validity_cells(),
          instance_root_shape(nullptr), constructor_thunk(nullptr),
          instance_default_inline_slot_count(
              _instance_default_inline_slot_count),
          instance_native_layout_id_(_instance_native_layout_id)
    {
        BuiltinInstanceShapeBuilder(
            this, BuiltinInstanceShapeDefaults::DunderClassAndDict, 0)
            .install(instance_shape_flags);

        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> dunder_dict_name = interned_string(L"__dict__");
        TValue<String> dunder_name_name = interned_string(L"__name__");
        TValue<String> dunder_bases_name = interned_string(L"__bases__");
        TValue<String> dunder_mro_name = interned_string(L"__mro__");
        TValue<String> dunder_new_name = interned_string(L"__new__");
        TValue<String> dunder_init_name = interned_string(L"__init__");
        DescriptorFlags class_metadata_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        DescriptorFlags class_value_flags =
            class_metadata_flags |
            descriptor_flag(DescriptorFlag::SpecialRead) |
            descriptor_flag(DescriptorFlag::SpecialMutate);
        DescriptorFlags class_dict_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot) |
            descriptor_flag(DescriptorFlag::SpecialRead) |
            descriptor_flag(DescriptorFlag::SpecialMutate);
        DescriptorFlags class_predefined_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[class_predefined_descriptor_count] = {
            ShapeRootDescriptor{
                dunder_class_name,
                DescriptorInfo::make(StorageLocation::not_found(),
                                     class_value_flags,
                                     DescriptorSpecialKind::ShapeClass)},
            ShapeRootDescriptor{
                dunder_dict_name,
                DescriptorInfo::make(StorageLocation::not_found(),
                                     class_dict_flags,
                                     DescriptorSpecialKind::SlotDict)},
            ShapeRootDescriptor{
                dunder_name_name,
                DescriptorInfo::make(StorageLocation{class_metadata_slot_name,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_bases_name,
                DescriptorInfo::make(StorageLocation{class_metadata_slot_bases,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_mro_name,
                DescriptorInfo::make(StorageLocation{class_metadata_slot_mro,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_new_name,
                DescriptorInfo::make(StorageLocation{class_predefined_slot_new,
                                                     StorageKind::Inline},
                                     class_predefined_flags)},
            ShapeRootDescriptor{
                dunder_init_name,
                DescriptorInfo::make(StorageLocation{class_predefined_slot_init,
                                                     StorageKind::Inline},
                                     class_predefined_flags)},
        };
        set_shape(Shape::make_root_with_descriptors(
            TValue<ClassObject>::from_oop(this), descriptors,
            class_predefined_descriptor_count, class_predefined_slot_count,
            class_metadata_slot_count + 2, class_inline_storage_slot_count,
            class_shape_flags));

        for(uint32_t slot_idx = 0;
            slot_idx < class_extra_inline_attribute_slot_count; ++slot_idx)
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
            Expected<TValue<Tuple>> computed_mro =
                compute_mro(this, assume_convert_to<Tuple>(bases_tuple));
            assert(computed_mro.has_value());
            mro = computed_mro.value().raw_value();
        }
    }

    ClassObject::ClassObject(ClassObject *metaclass, TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             ClassObject *single_base,
                             NativeLayoutId instance_native_layout_id,
                             ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : ClassObject(BootstrapObjectTag{}, _name,
                      _instance_default_inline_slot_count, single_base,
                      instance_native_layout_id, class_shape_flags,
                      instance_shape_flags)
    {
        install_bootstrap_class(metaclass);
    }

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             ClassObject *single_base,
                             NativeLayoutId instance_native_layout_id,
                             ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : ClassObject(active_vm()->type_class(), _name,
                      _instance_default_inline_slot_count, single_base,
                      instance_native_layout_id, class_shape_flags,
                      instance_shape_flags)
    {
    }

    Expected<TValue<ClassObject>> ClassObject::make(
        ThreadState *thread, ClassObject *metaclass, TValue<String> name,
        uint32_t instance_default_inline_slot_count, TValue<Tuple> bases,
        NativeLayoutId instance_native_layout_id, ShapeFlags class_shape_flags,
        ShapeFlags instance_shape_flags)
    {
        TValue<ClassObject> cls = thread->make_internal_value<ClassObject>(
            BootstrapObjectTag{}, name, instance_default_inline_slot_count,
            nullptr, instance_native_layout_id, class_shape_flags,
            instance_shape_flags);
        cls.extract()->install_bootstrap_class(metaclass);
        CL_TRY(validate_bases(bases));
        cls.extract()->bases = bases.raw_value();
        cls.extract()->mro =
            CL_TRY(compute_mro(cls.extract(), bases.extract())).raw_value();
        return Expected<TValue<ClassObject>>::ok(cls);
    }

    ClassObject *ClassObject::make_bootstrap_builtin_class(
        TValue<String> name, uint32_t instance_default_inline_slot_count,
        const BuiltinClassMethod *methods, uint32_t method_count,
        NativeLayoutId instance_native_layout_id)
    {
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType);
        ClassObject *type = active_vm()->type_class();
        assert(type != nullptr);
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            BootstrapObjectTag{}, name, instance_default_inline_slot_count,
            nullptr, instance_native_layout_id, class_shape_flags,
            fixed_attribute_shape_flags());
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
        ClassObject *single_base, NativeLayoutId instance_native_layout_id)
    {
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType);
        ClassObject *type = active_vm()->type_class();
        assert(type != nullptr);
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            type, name, instance_default_inline_slot_count, single_base,
            instance_native_layout_id, class_shape_flags,
            fixed_attribute_shape_flags());

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
        static_assert(sizeof(Member<TValue<String>>) == sizeof(Value));
        static_assert(sizeof(Member<Value>) == sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, name) == sizeof(SlotObject));
        static_assert(CL_OFFSETOF(ClassObject, bases) ==
                      CL_OFFSETOF(ClassObject, name) +
                          class_metadata_slot_bases * sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, mro) ==
                      CL_OFFSETOF(ClassObject, name) +
                          class_metadata_slot_mro * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, class_extra_inline_attribute_slots) ==
            CL_OFFSETOF(ClassObject, name) +
                class_metadata_slot_count * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, mro_shape_and_contents_validity_cell) ==
            CL_OFFSETOF(ClassObject, name) +
                class_inline_storage_slot_count * sizeof(Value));
        static_assert(
            CL_OFFSETOF(
                ClassObject,
                mro_shape_and_metaclass_mro_shape_and_contents_validity_cell) ==
            CL_OFFSETOF(ClassObject, name) +
                (class_inline_storage_slot_count + 1) * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, attached_mro_shape_validity_cells) ==
            CL_OFFSETOF(ClassObject, name) +
                (class_inline_storage_slot_count + 2) * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject,
                        attached_mro_shape_and_contents_validity_cells) ==
            CL_OFFSETOF(ClassObject, name) +
                (class_inline_storage_slot_count + 2 +
                 HeapPtrArray<ValidityCell>::embedded_value_count) *
                    sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, instance_root_shape) ==
            CL_OFFSETOF(ClassObject, name) +
                (class_inline_storage_slot_count + 2 +
                 2 * HeapPtrArray<ValidityCell>::embedded_value_count) *
                    sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, constructor_thunk) ==
            CL_OFFSETOF(ClassObject, name) +
                (class_inline_storage_slot_count + 2 +
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
            ClassObject::class_inline_storage_slot_count, nullptr,
            NativeLayoutId::ClassObject, class_shape_flags,
            fixed_attribute_shape_flags());
        cls->install_bootstrap_class(cls);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void ClassObject::install_instance_root_shape_from_builder(
        const ShapeRootDescriptor *descriptors, uint32_t descriptor_count,
        int32_t next_slot_index, uint32_t present_count, ShapeFlags shape_flags)
    {
        instance_root_shape = Shape::make_root_with_descriptors(
            TValue<ClassObject>::from_oop(this), descriptors, descriptor_count,
            next_slot_index, present_count, instance_default_inline_slot_count,
            shape_flags);
    }

    static DescriptorFlags dunder_class_descriptor_flags()
    {
        return descriptor_flag(DescriptorFlag::ReadOnly) |
               descriptor_flag(DescriptorFlag::StableSlot) |
               descriptor_flag(DescriptorFlag::SpecialRead) |
               descriptor_flag(DescriptorFlag::SpecialMutate);
    }

    static DescriptorFlags dunder_dict_descriptor_flags()
    {
        return descriptor_flag(DescriptorFlag::ReadOnly) |
               descriptor_flag(DescriptorFlag::StableSlot) |
               descriptor_flag(DescriptorFlag::SpecialRead) |
               descriptor_flag(DescriptorFlag::SpecialMutate);
    }

    BuiltinInstanceShapeBuilder::BuiltinInstanceShapeBuilder(
        ClassObject *_cls, BuiltinInstanceShapeDefaults defaults,
        uint32_t _predefined_slot_count)
        : cls(_cls), predefined_slot_count(_predefined_slot_count),
          descriptors(), declared_slots(_predefined_slot_count, false),
          declared_slot_count(0), declared_slot_index_sum(0)
    {
        assert(cls != nullptr);
        switch(defaults)
        {
            case BuiltinInstanceShapeDefaults::None:
                break;
            case BuiltinInstanceShapeDefaults::DunderClass:
            case BuiltinInstanceShapeDefaults::DunderClassAndDict:
                add_descriptor(
                    interned_string(L"__class__"),
                    DescriptorInfo::make(StorageLocation::not_found(),
                                         dunder_class_descriptor_flags(),
                                         DescriptorSpecialKind::ShapeClass));
                break;
        }
        if(defaults == BuiltinInstanceShapeDefaults::DunderClassAndDict)
        {
            add_descriptor(
                interned_string(L"__dict__"),
                DescriptorInfo::make(StorageLocation::not_found(),
                                     dunder_dict_descriptor_flags(),
                                     DescriptorSpecialKind::SlotDict));
        }
    }

    BuiltinInstanceShapeBuilder &
    BuiltinInstanceShapeBuilder::add_slot(const wchar_t *name,
                                          uint32_t slot_index)
    {
        return add_slot(interned_string(name), slot_index);
    }

    BuiltinInstanceShapeBuilder &
    BuiltinInstanceShapeBuilder::add_slot(TValue<String> name,
                                          uint32_t slot_index)
    {
        reserve_slot(slot_index);
        return add_descriptor(
            name, DescriptorInfo::make(
                      StorageLocation{int32_t(slot_index), StorageKind::Inline},
                      descriptor_flag(DescriptorFlag::StableSlot)));
    }

    BuiltinInstanceShapeBuilder &
    BuiltinInstanceShapeBuilder::reserve_slot(uint32_t slot_index)
    {
        assert(slot_index < predefined_slot_count);
        assert(!declared_slots[slot_index]);
        declared_slots[slot_index] = true;
        ++declared_slot_count;
        declared_slot_index_sum += slot_index;
        return *this;
    }

    BuiltinInstanceShapeBuilder &
    BuiltinInstanceShapeBuilder::add_descriptor(TValue<String> name,
                                                DescriptorInfo info)
    {
        descriptors.push_back(ShapeRootDescriptor{name, info});
        return *this;
    }

    void BuiltinInstanceShapeBuilder::install(ShapeFlags shape_flags)
    {
        for(const ShapeRootDescriptor &descriptor: descriptors)
        {
            DescriptorInfo info = descriptor.info;
            if(info.physical_idx < 0)
            {
                continue;
            }
            assert(info.kind == StorageKind::Inline);
            assert(uint32_t(info.physical_idx) < predefined_slot_count);
            assert(declared_slots[info.physical_idx]);
        }
        assert(declared_slot_count == predefined_slot_count);
        assert(declared_slot_index_sum == uint64_t(predefined_slot_count) *
                                              (predefined_slot_count - 1) / 2);
        cls->install_instance_root_shape_from_builder(
            descriptors.data(), uint32_t(descriptors.size()),
            int32_t(predefined_slot_count), uint32_t(descriptors.size()),
            shape_flags);
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

        ClassObject *metaclass = get_shape()->get_class();
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

        Value mro_value = inline_slot_base()[class_metadata_slot_mro];
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

        size_t reuse_scan_count =
            std::min(attached_cells.size(), kMaxAttachedValidityCellReuseScan);
        for(size_t idx = 0; idx < reuse_scan_count; ++idx)
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

    Expected<ConstructorThunkLookup>
    ClassObject::create_constructor_thunk_slow() const
    {
        ClassObject *self = const_cast<ClassObject *>(this);
        if(get_shape()->get_class() != active_vm()->type_class())
        {
            return Expected<ConstructorThunkLookup>::ok(
                ConstructorThunkLookup{nullptr, nullptr});
        }

        ValidityCell *lookup_cell =
            get_or_create_mro_shape_and_contents_validity_cell();
        Function *existing = constructor_thunk.extract();
        if(existing != nullptr && lookup_cell->is_valid())
        {
            return Expected<ConstructorThunkLookup>::ok(
                ConstructorThunkLookup{existing, lookup_cell});
        }

        TValue<String> new_name(interned_string(L"__new__"));
        AttributeReadDescriptor new_descriptor =
            resolve_attr_read_descriptor(Value::from_oop(self), new_name);

        TValue<String> init_name(interned_string(L"__init__"));
        AttributeReadDescriptor init_descriptor =
            resolve_attr_read_descriptor(Value::from_oop(self), init_name);
        if(!new_descriptor.is_found() && !init_descriptor.is_found())
        {
            TValue<Function> thunk =
                CL_TRY(make_init_only_constructor_thunk_function(
                    self, Optional<TValue<Function>>::none()));
            constructor_thunk = thunk.extract();
            return Expected<ConstructorThunkLookup>::ok(
                ConstructorThunkLookup{thunk.extract(), lookup_cell});
        }
        if(new_descriptor.is_found() && !init_descriptor.is_found())
        {
            if(!new_descriptor.is_cacheable() ||
               new_descriptor.plan.kind ==
                   AttributeReadPlanKind::DataDescriptorGet ||
               new_descriptor.plan.kind ==
                   AttributeReadPlanKind::NonDataDescriptorGet)
            {
                return Expected<ConstructorThunkLookup>::ok(
                    ConstructorThunkLookup{nullptr, nullptr});
            }

            Value new_value =
                load_attr_from_plan(Value::from_oop(self), new_descriptor.plan);
            if(!can_convert_to<Function>(new_value))
            {
                return Expected<ConstructorThunkLookup>::ok(
                    ConstructorThunkLookup{nullptr, nullptr});
            }

            TValue<Function> thunk =
                CL_TRY(make_new_only_constructor_thunk_function(
                    self, TValue<Function>::from_value_assumed(new_value)));
            constructor_thunk = thunk.extract();
            return Expected<ConstructorThunkLookup>::ok(
                ConstructorThunkLookup{thunk.extract(), lookup_cell});
        }
        if(new_descriptor.is_found())
        {
            return Expected<ConstructorThunkLookup>::ok(
                ConstructorThunkLookup{nullptr, nullptr});
        }

        if(!init_descriptor.is_cacheable() ||
           init_descriptor.plan.kind ==
               AttributeReadPlanKind::DataDescriptorGet ||
           init_descriptor.plan.kind ==
               AttributeReadPlanKind::NonDataDescriptorGet)
        {
            return Expected<ConstructorThunkLookup>::ok(
                ConstructorThunkLookup{nullptr, nullptr});
        }

        Value init_value =
            load_attr_from_plan(Value::from_oop(self), init_descriptor.plan);
        if(!can_convert_to<Function>(init_value))
        {
            return Expected<ConstructorThunkLookup>::ok(
                ConstructorThunkLookup{nullptr, nullptr});
        }

        TValue<Function> thunk =
            CL_TRY(make_init_only_constructor_thunk_function(
                self, Optional<TValue<Function>>::some(
                          TValue<Function>::from_value_assumed(init_value))));
        constructor_thunk = thunk.extract();
        return Expected<ConstructorThunkLookup>::ok(
            ConstructorThunkLookup{thunk.extract(), lookup_cell});
    }

    Value ClassObject::make_bases_tuple(ClassObject *single_base) const
    {
        assert(single_base != nullptr);
        Tuple *bases = make_object_raw<Tuple>(1);
        bases->initialize_item_unchecked(0, Value::from_oop(single_base));
        return Value::from_oop(bases);
    }

}  // namespace cl
