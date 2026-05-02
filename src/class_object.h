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
    class Function;
    class Tuple;

    enum class MroValidityCellInstallMode : uint8_t
    {
        IncludeSelf,
        SkipSelf,
    };

    enum class MroValidityCellDependency : uint8_t
    {
        ShapeOnly,
        ShapeAndContents,
    };

    struct BuiltinClassMethod
    {
        TValue<String> name;
        Value value;
    };

    struct ConstructorThunkLookup
    {
        Function *thunk;
        ValidityCell *lookup_cell;

        bool is_found() const { return thunk != nullptr; }
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
        static constexpr uint32_t kClassPredefinedSlotNew = 4;
        static constexpr uint32_t kClassPredefinedSlotInit = 5;
        static constexpr uint32_t kClassPredefinedSlotCount = 6;
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
        Value get_mro_value() const { return mro; }
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
        ValidityCell *current_mro_shape_and_contents_validity_cell() const
        {
            return mro_shape_and_contents_validity_cell.extract();
        }
        ValidityCell *
        current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell()
            const
        {
            return mro_shape_and_metaclass_mro_shape_and_contents_validity_cell
                .extract();
        }
        uint32_t attached_mro_shape_validity_cell_count() const
        {
            return attached_mro_shape_validity_cells.size();
        }
        uint32_t attached_mro_shape_and_contents_validity_cell_count() const
        {
            return attached_mro_shape_and_contents_validity_cells.size();
        }
        ALWAYSINLINE ValidityCell *
        get_or_create_mro_shape_and_contents_validity_cell() const
        {
            ValidityCell *cell = mro_shape_and_contents_validity_cell.extract();
            if(likely(cell != nullptr && cell->is_valid()))
            {
                return cell;
            }
            return create_mro_shape_and_contents_validity_cell_slow();
        }
        ALWAYSINLINE ValidityCell *
        get_or_create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell()
            const
        {
            ValidityCell *cell =
                mro_shape_and_metaclass_mro_shape_and_contents_validity_cell
                    .extract();
            if(likely(cell != nullptr && cell->is_valid()))
            {
                return cell;
            }
            return create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_slow();
        }
        void invalidate_lookup_validity_cells_for_shape_change();
        void invalidate_lookup_validity_cells_for_contents_change();
        Function *current_constructor_thunk() const
        {
            return constructor_thunk.extract();
        }
        ALWAYSINLINE ConstructorThunkLookup
        get_or_create_constructor_thunk() const
        {
            ValidityCell *cell = mro_shape_and_contents_validity_cell.extract();
            Function *thunk = constructor_thunk.extract();
            if(likely(thunk != nullptr && cell != nullptr && cell->is_valid()))
            {
                return ConstructorThunkLookup{thunk, cell};
            }
            return create_constructor_thunk_slow();
        }

    private:
        static constexpr uint32_t kClassExtraInlineAttributeSlotCount =
            kClassInlineStorageSlotCount - kClassMetadataSlotCount;

        Value make_bases_tuple(ClassObject *single_base) const;
        static void validate_bases(TValue<Tuple> bases);
        NOINLINE ValidityCell *
        create_mro_shape_and_contents_validity_cell_slow() const;
        NOINLINE ValidityCell *
        create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_slow()
            const;
        NOINLINE ConstructorThunkLookup create_constructor_thunk_slow() const;
        void install_validity_cell_along_mro(
            ValidityCell *cell, MroValidityCellInstallMode mode,
            MroValidityCellDependency dependency) const;
        void
        attach_mro_validity_cell(ValidityCell *cell,
                                 MroValidityCellDependency dependency) const;

        MemberTValue<String> name;
        MemberValue bases;
        MemberValue mro;
        MemberValue class_extra_inline_attribute_slots
            [kClassExtraInlineAttributeSlotCount];
        mutable MemberHeapPtr<ValidityCell>
            mro_shape_and_contents_validity_cell;
        mutable MemberHeapPtr<ValidityCell>
            mro_shape_and_metaclass_mro_shape_and_contents_validity_cell;
        mutable HeapPtrArray<ValidityCell> attached_mro_shape_validity_cells;
        mutable HeapPtrArray<ValidityCell>
            attached_mro_shape_and_contents_validity_cells;
        MemberHeapPtr<Shape> instance_root_shape;
        mutable MemberHeapPtr<Function> constructor_thunk;
        uint32_t instance_default_inline_slot_count;

    public:
        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(
            ClassObject, Object,
            3 + kClassExtraInlineAttributeSlotCount + 2 +
                2 * HeapPtrArray<ValidityCell>::embedded_value_count + 2);
    };

    class VirtualMachine;
    BuiltinClassDefinition make_type_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_CLASS_OBJECT_H
