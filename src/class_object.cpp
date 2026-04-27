#include "class_object.h"
#include "runtime_helpers.h"
#include "str.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <algorithm>
#include <deque>
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
          primary_lookup_validity_cell(nullptr), instance_root_shape(nullptr),
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
        DescriptorFlags class_metadata_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[kClassMetadataSlotCount] = {
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
        };
        set_shape(Shape::make_root_with_descriptors(
            Value::from_oop(this), descriptors, kClassMetadataSlotCount,
            kClassMetadataSlotCount, class_shape_flags));

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
        static_assert(CL_OFFSETOF(ClassObject, primary_lookup_validity_cell) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          kClassInlineStorageSlotCount * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, attached_lookup_validity_cells) ==
            CL_OFFSETOF(ClassObject, cls) +
                (kClassInlineStorageSlotCount + 1) * sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, instance_root_shape) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          (kClassInlineStorageSlotCount + 1 +
                           HeapPtrArray<ValidityCell>::embedded_value_count) *
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
        invalidate_lookup_validity_cells();
    }

    ValidityCell *ClassObject::create_lookup_validity_cell_slow() const
    {
        Value mro_value = inline_slot_base()[kClassMetadataSlotMro];
        Tuple *mro = assume_convert_to<Tuple>(mro_value);

        ValidityCell *cell = make_internal_raw<ValidityCell>();
        primary_lookup_validity_cell = cell;

        for(uint32_t mro_idx = 1; mro_idx < mro->size(); ++mro_idx)
        {
            ClassObject *base =
                assume_convert_to<ClassObject>(mro->item_unchecked(mro_idx));
            base->attach_lookup_validity_cell(cell);
        }

        return cell;
    }

    void ClassObject::attach_lookup_validity_cell(ValidityCell *cell) const
    {
        assert(cell != nullptr);
        assert(cell->is_valid());

        for(size_t idx = 0; idx < attached_lookup_validity_cells.size(); ++idx)
        {
            ValidityCell *attached_cell = attached_lookup_validity_cells[idx];
            if(!attached_cell->is_valid())
            {
                attached_lookup_validity_cells.set(idx, cell);
                return;
            }
        }

        attached_lookup_validity_cells.push_back(cell);
    }

    void ClassObject::invalidate_lookup_validity_cells()
    {
        for(ValidityCell *cell: attached_lookup_validity_cells)
        {
            cell->invalidate();
        }
        attached_lookup_validity_cells.clear();

        if(primary_lookup_validity_cell != nullptr)
        {
            primary_lookup_validity_cell->invalidate();
            primary_lookup_validity_cell = nullptr;
        }
    }

    AttributeReadDescriptor
    ClassObject::lookup_instance_attribute_descriptor(TValue<String> name,
                                                      Value receiver) const
    {
        return lookup_class_chain_descriptor(
            name, AttributeReadPlanPath::InstanceClassChain,
            AttributeBindingContext{receiver, this});
    }

    AttributeReadDescriptor
    ClassObject::lookup_class_attribute_descriptor(TValue<String> name) const
    {
        return lookup_class_chain_descriptor(
            name, AttributeReadPlanPath::ClassObjectChain,
            AttributeBindingContext{Value::None(), this});
    }

    AttributeReadDescriptor ClassObject::lookup_metaclass_attribute_descriptor(
        TValue<String> name, ClassObject *receiver_class) const
    {
        return lookup_class_chain_descriptor(
            name, AttributeReadPlanPath::MetaclassChain,
            AttributeBindingContext{Value::from_oop(receiver_class), this});
    }

    AttributeReadDescriptor ClassObject::lookup_class_chain_descriptor(
        TValue<String> name, AttributeReadPlanPath path,
        AttributeBindingContext binding) const
    {
        Value mro_value = inline_slot_base()[kClassMetadataSlotMro];
        if(!can_convert_to<Tuple>(mro_value))
        {
            StorageLocation own_location =
                get_shape()->resolve_present_property(name);
            if(!own_location.is_found())
            {
                return AttributeReadDescriptor::not_found();
            }

            Value own_value = read_storage_location(own_location);
            return AttributeReadDescriptor::found(
                AttributeReadPlan::from_storage(
                    path, attribute_read_plan_kind_for_path(path, own_value),
                    this, own_location, own_value, binding),
                attribute_cache_blockers_for_class_value(own_value));
        }

        ValidityCell *validity_cell = lookup_validity_cell();
        Tuple *mro = try_convert_to<Tuple>(mro_value);
        for(uint32_t mro_idx = 0; mro_idx < mro->size(); ++mro_idx)
        {
            Value class_value = mro->item_unchecked(mro_idx);
            ClassObject *cls = try_convert_to<ClassObject>(class_value);
            if(cls == nullptr)
            {
                continue;
            }

            DescriptorLookup lookup =
                cls->get_shape()->lookup_descriptor_including_latent(name);
            if(!lookup.is_present())
            {
                continue;
            }

            Value value = cls->read_storage_location(lookup.storage_location());
            return AttributeReadDescriptor::found(
                AttributeReadPlan::from_storage(
                    path, attribute_read_plan_kind_for_path(path, value), cls,
                    lookup.storage_location(), value, binding, validity_cell),
                attribute_cache_blockers_for_class_value(value));
        }

        return AttributeReadDescriptor::not_found();
    }

    Value ClassObject::lookup_class_chain(TValue<String> name) const
    {
        AttributeReadDescriptor descriptor =
            lookup_class_attribute_descriptor(name);
        if(!descriptor.is_found())
        {
            return Value::not_present();
        }
        return descriptor.plan.value;
    }

    Value ClassObject::make_bases_tuple(ClassObject *single_base) const
    {
        assert(single_base != nullptr);
        Tuple *bases = make_object_raw<Tuple>(1);
        bases->initialize_item_unchecked(0, Value::from_oop(single_base));
        return Value::from_oop(bases);
    }

}  // namespace cl
