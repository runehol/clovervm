#ifndef CL_OBJECT_H
#define CL_OBJECT_H

#include "memory/native_layout_declarations.h"
#include "object_model/heap_object.h"
#include "object_model/heap_ptr.h"
#include "object_model/shape_descriptor.h"
#include "util/compiler.h"

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace cl
{
    class AttributeDeleteDescriptor;
    class AttributeReadDescriptor;
    class AttributeWriteDescriptor;
    struct BuiltinClassDefinition;
    class ClassObject;
    class OverflowSlots;
    class Shape;
    class InlineSlotObject;
    class VirtualMachine;
    class String;
    class Value;
    template <typename T> class TValue;
    template <typename T, bool> class Expected;

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
            : HeapObject(_native_layout_id), shape(nullptr),
              overflow_storage(nullptr)
        {
            initialize_shape_for_class(_cls, _native_layout_id);
        }

        Object(BootstrapObjectTag, NativeLayoutId _native_layout_id)
            : HeapObject(_native_layout_id), shape(nullptr),
              overflow_storage(nullptr)
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
        [[nodiscard]] Expected<bool, false>
        set_existing_own_property(TValue<String> name, Value value);
        [[nodiscard]] Expected<bool, false>
        set_own_property(TValue<String> name, Value value);
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
        MemberHeapPtr<OverflowSlots> overflow_storage;

        CL_DECLARE_STATIC_VALUE_SPAN(Object, shape, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(Object);

    private:
        ALWAYSINLINE InlineSlotObject *as_inline_slot_object();
        ALWAYSINLINE const InlineSlotObject *as_inline_slot_object() const;
        ALWAYSINLINE OverflowSlots *get_overflow_slots() const
        {
            return overflow_storage.get();
        }
        void ensure_storage_for_shape(Shape *new_shape);
        OverflowSlots *ensure_overflow_slot(int32_t physical_idx);

    protected:
        ALWAYSINLINE void initialize_shape(Shape *instance_root_shape,
                                           NativeLayoutId native_layout_id);
        void initialize_shape_for_class(ClassObject *class_object,
                                        NativeLayoutId native_layout_id);
    };

    constexpr bool native_layout_has_inline_slots(NativeLayoutId native_layout)
    {
        return native_layout == NativeLayoutId::Instance ||
               native_layout == NativeLayoutId::ClassObject ||
               native_layout == NativeLayoutId::Function ||
               native_layout == NativeLayoutId::Slice ||
               native_layout == NativeLayoutId::FileIO ||
               native_layout == NativeLayoutId::ModuleObject ||
               native_layout == NativeLayoutId::ModuleLoaderObject ||
               native_layout == NativeLayoutId::ModuleSpecObject ||
               native_layout == NativeLayoutId::Exception ||
               native_layout == NativeLayoutId::StopIteration ||
               native_layout == NativeLayoutId::OSError;
    }

    class InlineSlotObject : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;

        InlineSlotObject(ClassObject *cls, NativeLayoutId native_layout_id)
            : Object(BootstrapObjectTag{}, native_layout_id)
        {
            initialize_shape_for_class(cls, native_layout_id);
        }

        InlineSlotObject(Shape *initial_shape, NativeLayoutId native_layout_id);

        InlineSlotObject(BootstrapObjectTag tag,
                         NativeLayoutId native_layout_id)
            : Object(tag, native_layout_id)
        {
        }

        ALWAYSINLINE Value *inline_slot_base()
        {
            return reinterpret_cast<Value *>(
                reinterpret_cast<uint64_t *>(this) +
                sizeof(InlineSlotObject) / sizeof(uint64_t));
        }

        ALWAYSINLINE const Value *inline_slot_base() const
        {
            return reinterpret_cast<const Value *>(
                reinterpret_cast<const uint64_t *>(this) +
                sizeof(InlineSlotObject) / sizeof(uint64_t));
        }

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(InlineSlotObject, Object, 0);
        CL_DECLARE_STATIC_OBJECT_SIZE(InlineSlotObject);
    };

    ALWAYSINLINE InlineSlotObject *Object::as_inline_slot_object()
    {
        assert(native_layout_has_inline_slots(native_layout_id()));
        return static_cast<InlineSlotObject *>(this);
    }

    ALWAYSINLINE const InlineSlotObject *Object::as_inline_slot_object() const
    {
        assert(native_layout_has_inline_slots(native_layout_id()));
        return static_cast<const InlineSlotObject *>(this);
    }

    ALWAYSINLINE Value *Object::inline_slot_base()
    {
        return as_inline_slot_object()->InlineSlotObject::inline_slot_base();
    }

    ALWAYSINLINE const Value *Object::inline_slot_base() const
    {
        return as_inline_slot_object()->InlineSlotObject::inline_slot_base();
    }

    template <typename T>
    concept HasNativeLayoutIdConcept = requires { T::native_layout; };

    template <typename T>
    struct HasNativeLayoutId : std::bool_constant<HasNativeLayoutIdConcept<T>>
    {
    };

    template <typename T> bool has_native_layout(const Object *object)
    {
        static_assert(std::is_base_of_v<Object, T>);
        static_assert(HasNativeLayoutId<T>::value);
        return object != nullptr &&
               object->native_layout_id() == T::native_layout;
    }

    template <typename T> T *try_native_cast(Object *object)
    {
        return has_native_layout<T>(object) ? static_cast<T *>(object)
                                            : nullptr;
    }

    template <typename T> const T *try_native_cast(const Object *object)
    {
        return has_native_layout<T>(object) ? static_cast<const T *>(object)
                                            : nullptr;
    }

    template <typename T> T *native_cast(Object *object)
    {
        assert(has_native_layout<T>(object));
        return static_cast<T *>(object);
    }

    template <typename T> const T *native_cast(const Object *object)
    {
        assert(has_native_layout<T>(object));
        return static_cast<const T *>(object);
    }

    static_assert(CL_OFFSETOF(Object, overflow_storage) ==
                  CL_OFFSETOF(Object, shape) + sizeof(Shape *));
    static_assert(sizeof(Object) == 24);
    static_assert(sizeof(InlineSlotObject) == 24);
    static_assert(std::is_trivially_destructible_v<Object>);
    static_assert(std::is_trivially_destructible_v<InlineSlotObject>);

    BuiltinClassDefinition make_object_class(VirtualMachine *vm);
    void install_object_class_methods(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_OBJECT_H
