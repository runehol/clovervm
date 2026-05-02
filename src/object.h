#ifndef CL_OBJECT_H
#define CL_OBJECT_H

#include "heap_object.h"
#include "native_layout_id.h"
#include "shape_descriptor.h"

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace cl
{
    struct AttributeDeleteDescriptor;
    struct AttributeReadDescriptor;
    struct AttributeWriteDescriptor;
    struct BuiltinClassDefinition;
    class ClassObject;
    class OverflowSlots;
    class Shape;
    class VirtualMachine;
    struct String;
    struct Value;
    template <typename T> class TValue;

    struct BootstrapObjectTag
    {
    };

    /*
      Base class for Python-visible objects. These are heap records that also
      have Python class identity and concrete native layout identity.
    */
    struct Object : public HeapObject
    {
        Object(ClassObject *_cls, NativeLayoutId _native_layout_id,
               HeapLayout _layout)
            : HeapObject(_layout), native_layout(_native_layout_id),
              shape(nullptr), overflow_storage(nullptr), cls(nullptr)
        {
            install_class(_cls);
            initialize_shape_for_class(_cls);
        }

        Object(BootstrapObjectTag, NativeLayoutId _native_layout_id,
               HeapLayout _layout)
            : HeapObject(_layout), native_layout(_native_layout_id),
              shape(nullptr), overflow_storage(nullptr), cls(nullptr)
        {
        }

        void install_bootstrap_class(ClassObject *new_cls)
        {
            assert(cls == nullptr);
            install_class(new_cls);
        }

        NativeLayoutId native_layout_id() const { return native_layout; }
        TValue<ClassObject> get_class() const;
        bool is_class_bootstrapped() const { return cls != nullptr; }
        Shape *get_shape() const { return shape; }
        void set_shape(Shape *new_shape);
        Value get_own_property(TValue<String> name) const;
        AttributeReadDescriptor
        lookup_own_attribute_descriptor(TValue<String> name) const;
        AttributeWriteDescriptor
        lookup_own_attribute_write_descriptor(TValue<String> name);
        AttributeDeleteDescriptor
        lookup_own_attribute_delete_descriptor(TValue<String> name);
        bool add_own_property(TValue<String> name, Value value);
        bool define_own_property(TValue<String> name, Value value,
                                 DescriptorFlags descriptor_flags);
        bool set_existing_own_property(TValue<String> name, Value value);
        bool set_own_property(TValue<String> name, Value value);
        bool delete_own_property(TValue<String> name);
        Value read_storage_location(StorageLocation location) const;
        void write_storage_location(StorageLocation location, Value value);
        static void validate_inline_slot_layout();
        NativeLayoutId native_layout;
        Shape *shape;
        OverflowSlots *overflow_storage;
        ClassObject *cls;

        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(Object, cls, 1);

    protected:
        Value *inline_slot_base() { return reinterpret_cast<Value *>(&cls); }
        const Value *inline_slot_base() const
        {
            return reinterpret_cast<const Value *>(&cls);
        }

    private:
        void install_class(ClassObject *new_cls);
        void initialize_shape_for_class(ClassObject *class_object);
        void initialize_shape(Shape *instance_root_shape);
        OverflowSlots *get_overflow_slots() const { return overflow_storage; }
        OverflowSlots *ensure_overflow_slot(int32_t physical_idx);
    };

    template <typename T, typename = void>
    struct HasNativeLayoutId : std::false_type
    {
    };

    template <typename T>
    struct HasNativeLayoutId<T, std::void_t<decltype(T::native_layout_id)>>
        : std::true_type
    {
    };

    template <typename T> bool can_convert_to(const Object *object)
    {
        static_assert(std::is_base_of_v<Object, T>);
        static_assert(HasNativeLayoutId<T>::value);
        return object != nullptr &&
               object->native_layout_id() == T::native_layout_id;
    }

    template <typename T> T *try_convert_to(Object *object)
    {
        return can_convert_to<T>(object) ? static_cast<T *>(object) : nullptr;
    }

    template <typename T> const T *try_convert_to(const Object *object)
    {
        return can_convert_to<T>(object) ? static_cast<const T *>(object)
                                         : nullptr;
    }

    template <typename T> T *assume_convert_to(Object *object)
    {
        assert(can_convert_to<T>(object));
        return static_cast<T *>(object);
    }

    template <typename T> const T *assume_convert_to(const Object *object)
    {
        assert(can_convert_to<T>(object));
        return static_cast<const T *>(object);
    }

    static_assert(sizeof(Object) == 40);
    static_assert(std::is_trivially_destructible_v<Object>);

    BuiltinClassDefinition make_object_class(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_OBJECT_H
