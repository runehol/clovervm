#ifndef CL_OBJECT_H
#define CL_OBJECT_H

#include "native/native_layout_declarations.h"
#include "object_model/heap_object.h"
#include "object_model/shape_descriptor.h"

#include <cassert>
#include <cstdint>
#include <type_traits>

#ifndef ALWAYSINLINE
#define ALWAYSINLINE inline __attribute__((always_inline))
#endif
#ifndef INLINE
#define INLINE inline
#endif

namespace cl
{
    class AttributeDeleteDescriptor;
    class AttributeReadDescriptor;
    class AttributeWriteDescriptor;
    struct BuiltinClassDefinition;
    class ClassObject;
    class OverflowSlots;
    class Shape;
    class SlotObject;
    class VirtualMachine;
    class String;
    class Value;
    template <typename T> class TValue;

    struct BootstrapObjectTag
    {
    };

    /*
      Base class for Python-visible objects. These are heap records that also
      have Python class identity and concrete native layout identity.
    */
    class Object : public HeapObject
    {
    public:
        Object(ClassObject *_cls, NativeLayoutId _native_layout_id)
            : HeapObject(_native_layout_id), shape(nullptr)
        {
            initialize_shape_for_class(_cls, _native_layout_id);
        }

        Object(BootstrapObjectTag, NativeLayoutId _native_layout_id)
            : HeapObject(_native_layout_id), shape(nullptr)
        {
        }

        void install_bootstrap_class(ClassObject *new_cls);

        bool is_class_bootstrapped() const { return shape != nullptr; }
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
        HeapObject *write_existing_storage_location_returning_zero_ref(
            StorageLocation location, Value value);
        void write_existing_storage_location(StorageLocation location,
                                             Value value);
        // Writes a storage location whose previous logical value is known to
        // be absent. Instance inline writes may initialize slots up to and
        // including this location; existing-value overwrites and deletes must
        // use write_storage_location instead so the old value is released.
        void write_empty_storage_location(StorageLocation location,
                                          Value value);
        void write_storage_location(StorageLocation location, Value value);
        ALWAYSINLINE Value *inline_slot_base();
        ALWAYSINLINE const Value *inline_slot_base() const;
        Shape *shape;

        CL_DECLARE_STATIC_VALUE_SPAN(Object, shape, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(Object);

    private:
        ALWAYSINLINE void initialize_shape(Shape *instance_root_shape,
                                           NativeLayoutId native_layout_id);
        ALWAYSINLINE SlotObject *as_slot_object();
        ALWAYSINLINE const SlotObject *as_slot_object() const;
        ALWAYSINLINE OverflowSlots *get_overflow_slots() const;
        void ensure_storage_for_shape(Shape *new_shape);
        OverflowSlots *ensure_overflow_slot(int32_t physical_idx);

    protected:
        void initialize_shape_for_class(ClassObject *class_object,
                                        NativeLayoutId native_layout_id);
    };

    constexpr bool native_layout_has_slots(NativeLayoutId native_layout)
    {
        return native_layout == NativeLayoutId::Instance ||
               native_layout == NativeLayoutId::ClassObject ||
               native_layout == NativeLayoutId::Function ||
               native_layout == NativeLayoutId::Slice ||
               native_layout == NativeLayoutId::ModuleObject ||
               native_layout == NativeLayoutId::ModuleLoaderObject ||
               native_layout == NativeLayoutId::ModuleSpecObject ||
               native_layout == NativeLayoutId::Exception ||
               native_layout == NativeLayoutId::StopIteration;
    }

    class SlotObject : public Object
    {
    public:
        SlotObject(ClassObject *cls, NativeLayoutId native_layout_id)
            : Object(BootstrapObjectTag{}, native_layout_id),
              overflow_storage(nullptr)
        {
            initialize_shape_for_class(cls, native_layout_id);
        }

        SlotObject(BootstrapObjectTag tag, NativeLayoutId native_layout_id)
            : Object(tag, native_layout_id), overflow_storage(nullptr)
        {
        }

        ALWAYSINLINE Value *inline_slot_base()
        {
            return reinterpret_cast<Value *>(
                reinterpret_cast<uint64_t *>(this) +
                sizeof(SlotObject) / sizeof(uint64_t));
        }

        ALWAYSINLINE const Value *inline_slot_base() const
        {
            return reinterpret_cast<const Value *>(
                reinterpret_cast<const uint64_t *>(this) +
                sizeof(SlotObject) / sizeof(uint64_t));
        }

        ALWAYSINLINE OverflowSlots *get_overflow_slots() const
        {
            return overflow_storage;
        }
        void ensure_storage_for_shape(Shape *new_shape);
        OverflowSlots *ensure_overflow_slot(int32_t physical_idx);

        OverflowSlots *overflow_storage;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(SlotObject, Object, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(SlotObject);
    };

    ALWAYSINLINE SlotObject *Object::as_slot_object()
    {
        assert(native_layout_has_slots(native_layout_id()));
        return static_cast<SlotObject *>(this);
    }

    ALWAYSINLINE const SlotObject *Object::as_slot_object() const
    {
        assert(native_layout_has_slots(native_layout_id()));
        return static_cast<const SlotObject *>(this);
    }

    ALWAYSINLINE Value *Object::inline_slot_base()
    {
        return as_slot_object()->SlotObject::inline_slot_base();
    }

    ALWAYSINLINE const Value *Object::inline_slot_base() const
    {
        return as_slot_object()->SlotObject::inline_slot_base();
    }

    ALWAYSINLINE OverflowSlots *Object::get_overflow_slots() const
    {
        return as_slot_object()->SlotObject::get_overflow_slots();
    }

    inline void Object::ensure_storage_for_shape(Shape *new_shape)
    {
        as_slot_object()->SlotObject::ensure_storage_for_shape(new_shape);
    }

    inline OverflowSlots *Object::ensure_overflow_slot(int32_t physical_idx)
    {
        return as_slot_object()->SlotObject::ensure_overflow_slot(physical_idx);
    }

    template <typename T, typename = void>
    struct HasNativeLayoutId : std::false_type
    {
    };

    template <typename T>
    struct HasNativeLayoutId<T, std::void_t<decltype(T::native_layout)>>
        : std::true_type
    {
    };

    template <typename T> bool can_convert_to(const Object *object)
    {
        static_assert(std::is_base_of_v<Object, T>);
        static_assert(HasNativeLayoutId<T>::value);
        return object != nullptr &&
               object->native_layout_id() == T::native_layout;
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

    static_assert(sizeof(Object) == 16);
    static_assert(sizeof(SlotObject) == 24);
    static_assert(std::is_trivially_destructible_v<Object>);
    static_assert(std::is_trivially_destructible_v<SlotObject>);

    BuiltinClassDefinition make_object_class(VirtualMachine *vm);
    void install_object_class_methods(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_OBJECT_H
