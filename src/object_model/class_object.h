#ifndef CL_CLASS_OBJECT_H
#define CL_CLASS_OBJECT_H

#include "memory/heap_construction.h"
#include "object_model/attribute_descriptor.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/instance.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/refcount.h"
#include "object_model/shape.h"
#include "object_model/typed_value.h"
#include "object_model/validity_cell.h"
#include "object_model/value.h"
#include "object_model/vm_array.h"
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject;
    class Function;
    class ThreadState;
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

    enum class BuiltinInstanceShapeDefaults
    {
        None,
        DunderClass,
        DunderClassAndDict,
    };

    class BuiltinInstanceShapeBuilder
    {
    public:
        BuiltinInstanceShapeBuilder(ClassObject *cls,
                                    BuiltinInstanceShapeDefaults defaults,
                                    uint32_t predefined_slot_count);

        BuiltinInstanceShapeBuilder &add_slot(const wchar_t *name,
                                              uint32_t slot_index);
        BuiltinInstanceShapeBuilder &add_slot(TValue<String> name,
                                              uint32_t slot_index);
        BuiltinInstanceShapeBuilder &add_slot(TValue<String> name,
                                              uint32_t slot_index,
                                              DescriptorFlags flags);
        BuiltinInstanceShapeBuilder &reserve_slot(uint32_t slot_index);
        BuiltinInstanceShapeBuilder &add_descriptor(TValue<String> name,
                                                    DescriptorInfo info);

        void install(ShapeFlags shape_flags);

    private:
        ClassObject *cls;
        uint32_t predefined_slot_count;
        std::vector<ShapeRootDescriptor> descriptors;
        std::vector<bool> declared_slots;
        uint32_t declared_slot_count;
        uint64_t declared_slot_index_sum;
    };

    struct ConstructorThunkLookup
    {
        Function *thunk;
        ValidityCell *lookup_cell;

        bool is_found() const { return thunk != nullptr; }
    };

    class ClassObject : public SlotObject
    {
    public:
        friend class BuiltinInstanceShapeBuilder;

        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ClassObject;
        static constexpr uint32_t class_metadata_slot_name = 0;
        static constexpr uint32_t class_metadata_slot_bases = 1;
        static constexpr uint32_t class_metadata_slot_mro = 2;
        static constexpr uint32_t class_metadata_slot_count = 3;
        static constexpr uint32_t class_predefined_slot_new = 3;
        static constexpr uint32_t class_predefined_slot_init = 4;
        static constexpr uint32_t class_predefined_slot_count = 5;
        static constexpr uint32_t class_predefined_descriptor_count = 7;
        static constexpr uint32_t class_inline_storage_slot_count = 48;

        ClassObject(
            BootstrapObjectTag, TValue<String> name,
            uint32_t instance_default_inline_slot_count,
            ClassObject *single_base, NativeLayoutId instance_native_layout_id,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        ClassObject(
            ClassObject *metaclass, TValue<String> name,
            uint32_t instance_default_inline_slot_count,
            ClassObject *single_base, NativeLayoutId instance_native_layout_id,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        ClassObject(
            TValue<String> name, uint32_t instance_default_inline_slot_count,
            ClassObject *single_base, NativeLayoutId instance_native_layout_id,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        static Expected<TValue<ClassObject>> make(
            ThreadState *thread, ClassObject *metaclass, TValue<String> name,
            uint32_t instance_default_inline_slot_count, TValue<Tuple> bases,
            NativeLayoutId instance_native_layout_id,
            ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject),
            ShapeFlags instance_shape_flags = mutable_attribute_shape_flags());

        template <typename T>
        static ClassObject *make_bootstrap_builtin_class(
            TValue<String> name, uint32_t instance_default_inline_slot_count,
            const BuiltinClassMethod *methods, uint32_t method_count)
        {
            static_assert(HasNativeLayoutId<T>::value);
            return make_bootstrap_builtin_class(
                name, instance_default_inline_slot_count, methods, method_count,
                T::native_layout);
        }

        static ClassObject *make_builtin_class(
            TValue<String> name, uint32_t instance_default_inline_slot_count,
            const BuiltinClassMethod *methods, uint32_t method_count,
            ClassObject *single_base, NativeLayoutId instance_native_layout_id);
        template <typename T>
        static ClassObject *
        make_builtin_class(TValue<String> name,
                           uint32_t instance_default_inline_slot_count,
                           const BuiltinClassMethod *methods,
                           uint32_t method_count, ClassObject *single_base)
        {
            static_assert(HasNativeLayoutId<T>::value);
            return make_builtin_class(name, instance_default_inline_slot_count,
                                      methods, method_count, single_base,
                                      T::native_layout);
        }

        TValue<String> get_name() const { return name.value(); }
        Value get_mro_value() const { return mro; }
        uint32_t get_instance_default_inline_slot_count() const
        {
            return instance_default_inline_slot_count;
        }
        NativeLayoutId instance_native_layout_id() const
        {
            return instance_native_layout_id_;
        }
        static void validate_inline_slot_layout();
        void install_bootstrap_inheritance(Value bases_tuple, Value mro_tuple);
        Shape *get_instance_root_shape() const
        {
            return instance_root_shape.extract();
        }
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
        ALWAYSINLINE Expected<ConstructorThunkLookup>
        get_or_create_constructor_thunk() const
        {
            ValidityCell *cell = mro_shape_and_contents_validity_cell.extract();
            Function *thunk = constructor_thunk.extract();
            if(likely(thunk != nullptr && cell != nullptr && cell->is_valid()))
            {
                return Expected<ConstructorThunkLookup>::ok(
                    ConstructorThunkLookup{thunk, cell});
            }
            return create_constructor_thunk_slow();
        }

    private:
        static constexpr uint32_t class_extra_inline_attribute_slot_count =
            class_inline_storage_slot_count - class_metadata_slot_count;

        void install_instance_root_shape_from_builder(
            const ShapeRootDescriptor *descriptors, uint32_t descriptor_count,
            int32_t next_slot_index, uint32_t present_count,
            ShapeFlags shape_flags);

        Value make_bases_tuple(ClassObject *single_base) const;
        static Expected<void> validate_bases(TValue<Tuple> bases);
        NOINLINE ValidityCell *
        create_mro_shape_and_contents_validity_cell_slow() const;
        NOINLINE ValidityCell *
        create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_slow()
            const;
        NOINLINE Expected<ConstructorThunkLookup>
        create_constructor_thunk_slow() const;
        void install_validity_cell_along_mro(
            ValidityCell *cell, MroValidityCellInstallMode mode,
            MroValidityCellDependency dependency) const;
        void
        attach_mro_validity_cell(ValidityCell *cell,
                                 MroValidityCellDependency dependency) const;

        Member<TValue<String>> name;
        Member<Value> bases;
        Member<Value> mro;
        Value class_extra_inline_attribute_slots
            [class_extra_inline_attribute_slot_count];
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
        NativeLayoutId instance_native_layout_id_;

        static ClassObject *make_bootstrap_builtin_class(
            TValue<String> name, uint32_t instance_default_inline_slot_count,
            const BuiltinClassMethod *methods, uint32_t method_count,
            NativeLayoutId instance_native_layout_id);

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            ClassObject, SlotObject,
            3 + class_extra_inline_attribute_slot_count + 2 +
                2 * HeapPtrArray<ValidityCell>::embedded_value_count + 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(ClassObject);
    };

    class VirtualMachine;
    ALWAYSINLINE void
    Object::initialize_shape_for_class(ClassObject *class_object,
                                       NativeLayoutId native_layout_id)
    {
        assert(class_object != nullptr);
        initialize_shape(class_object->get_instance_root_shape(),
                         native_layout_id);
    }

    ALWAYSINLINE void Object::initialize_shape(Shape *instance_root_shape,
                                               NativeLayoutId native_layout_id)
    {
        assert(shape == nullptr);
        assert(instance_root_shape != nullptr);

        shape = incref(instance_root_shape);
        if(!native_layout_has_slots(native_layout_id) ||
           native_layout_id == NativeLayoutId::Instance)
        {
            return;
        }

        int32_t next_slot_index = instance_root_shape->get_next_slot_index();
        assert(next_slot_index >= 0);
        assert(uint32_t(next_slot_index) <=
               instance_root_shape->get_inline_slot_count());

        for(uint32_t slot_idx = 0; slot_idx < uint32_t(next_slot_index);
            ++slot_idx)
        {
            inline_slot_base()[slot_idx] = Value::not_present();
        }
    }

    bool is_subclass_of(ClassObject *cls, ClassObject *base);
    bool is_instance_of(Object *obj, ClassObject *cls);
    BuiltinClassDefinition make_type_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_CLASS_OBJECT_H
