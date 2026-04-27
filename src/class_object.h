#ifndef CL_CLASS_OBJECT_H
#define CL_CLASS_OBJECT_H

#include "attribute_descriptor.h"
#include "builtin_class_registry.h"
#include "instance.h"
#include "object.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "shape.h"
#include "typed_value.h"
#include "validity_cell.h"
#include "value.h"
#include "vm_array.h"
#include <cstdint>

namespace cl
{
    class Tuple;

    enum class MroValidityCellInstallMode : uint8_t
    {
        IncludeSelf,
        SkipSelf,
    };

    struct BuiltinClassMethod
    {
        TValue<String> name;
        Value value;
    };

    class ClassObject : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::ClassObject;
        static constexpr uint32_t kClassMetadataSlotClass = 0;
        static constexpr uint32_t kClassMetadataSlotName = 1;
        static constexpr uint32_t kClassMetadataSlotBases = 2;
        static constexpr uint32_t kClassMetadataSlotMro = 3;
        static constexpr uint32_t kClassMetadataSlotCount = 4;
        static constexpr uint32_t kClassInlineStorageSlotCount = 48;

        ClassObject(
            BootstrapObjectTag, TValue<String> name,
            uint32_t instance_default_inline_slot_count,
            ClassObject *single_base,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        ClassObject(
            ClassObject *metaclass, TValue<String> name,
            uint32_t instance_default_inline_slot_count,
            ClassObject *single_base,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        ClassObject(
            ClassObject *metaclass, TValue<String> name,
            uint32_t instance_default_inline_slot_count, TValue<Tuple> bases,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        ClassObject(
            TValue<String> name, uint32_t instance_default_inline_slot_count,
            ClassObject *single_base,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        static ClassObject *make_bootstrap_builtin_class(
            TValue<String> name, uint32_t instance_default_inline_slot_count,
            const BuiltinClassMethod *methods, uint32_t method_count);

        static ClassObject *
        make_builtin_class(TValue<String> name,
                           uint32_t instance_default_inline_slot_count,
                           const BuiltinClassMethod *methods,
                           uint32_t method_count, ClassObject *single_base);

        TValue<String> get_name() const { return name; }
        uint32_t get_instance_default_inline_slot_count() const
        {
            return instance_default_inline_slot_count;
        }
        uint32_t get_class_inline_storage_slot_count() const
        {
            return kClassInlineStorageSlotCount;
        }
        static void validate_inline_slot_layout();
        void install_bootstrap_inheritance(Value bases_tuple, Value mro_tuple);
        Shape *get_instance_root_shape() const;
        ValidityCell *current_mro_validity_cell() const
        {
            return mro_validity_cell.extract();
        }
        ValidityCell *current_mro_and_metaclass_mro_validity_cell() const
        {
            return mro_and_metaclass_mro_validity_cell.extract();
        }
        uint32_t attached_lookup_validity_cell_count() const
        {
            return attached_lookup_validity_cells.size();
        }
        ALWAYSINLINE ValidityCell *get_or_create_mro_validity_cell() const
        {
            ValidityCell *cell = mro_validity_cell.extract();
            if(likely(cell != nullptr && cell->is_valid()))
            {
                return cell;
            }
            return create_mro_validity_cell_slow();
        }
        ALWAYSINLINE ValidityCell *
        get_or_create_mro_and_metaclass_mro_validity_cell() const
        {
            ValidityCell *cell = mro_and_metaclass_mro_validity_cell.extract();
            if(likely(cell != nullptr && cell->is_valid()))
            {
                return cell;
            }
            return create_mro_and_metaclass_mro_validity_cell_slow();
        }
        void invalidate_lookup_validity_cells();

        AttributeReadDescriptor
        lookup_instance_attribute_descriptor(TValue<String> name,
                                             Value receiver) const;
        AttributeReadDescriptor
        lookup_class_attribute_descriptor(TValue<String> name) const;
        AttributeReadDescriptor lookup_metaclass_attribute_descriptor(
            TValue<String> name, ClassObject *receiver_class) const;
        Value lookup_class_chain(TValue<String> name) const;

    private:
        static constexpr uint32_t kClassExtraInlineAttributeSlotCount =
            kClassInlineStorageSlotCount - kClassMetadataSlotCount;

        Value make_bases_tuple(ClassObject *single_base) const;
        static void validate_bases(TValue<Tuple> bases);
        NOINLINE ValidityCell *create_mro_validity_cell_slow() const;
        NOINLINE ValidityCell *
        create_mro_and_metaclass_mro_validity_cell_slow() const;
        void
        install_validity_cell_along_mro(ValidityCell *cell,
                                        MroValidityCellInstallMode mode) const;
        void attach_lookup_validity_cell(ValidityCell *cell) const;
        AttributeReadDescriptor
        lookup_class_chain_descriptor(TValue<String> name,
                                      AttributeReadPlanPath path,
                                      AttributeBindingContext binding) const;

        MemberTValue<String> name;
        MemberValue bases;
        MemberValue mro;
        MemberValue class_extra_inline_attribute_slots
            [kClassExtraInlineAttributeSlotCount];
        mutable MemberHeapPtr<ValidityCell> mro_validity_cell;
        mutable MemberHeapPtr<ValidityCell> mro_and_metaclass_mro_validity_cell;
        mutable HeapPtrArray<ValidityCell> attached_lookup_validity_cells;
        MemberHeapPtr<Shape> instance_root_shape;
        uint32_t instance_default_inline_slot_count;

    public:
        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(
            ClassObject, Object,
            3 + kClassExtraInlineAttributeSlotCount + 2 +
                HeapPtrArray<ValidityCell>::embedded_value_count + 1);
    };

    class VirtualMachine;
    BuiltinClassDefinition make_type_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_CLASS_OBJECT_H
