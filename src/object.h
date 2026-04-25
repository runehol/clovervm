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
    class ClassObject;
    class OverflowSlots;
    class Shape;
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
        bool set_own_property(TValue<String> name, Value value);
        bool delete_own_property(TValue<String> name);
        Value read_storage_location(StorageLocation location) const;
        void write_storage_location(StorageLocation location, Value value);
        static constexpr uint32_t static_value_offset_in_words()
        {
            static_assert(CL_OFFSETOF(Object, cls) % sizeof(uint64_t) == 0,
                          "Value region must start on a 64-bit word boundary");
            return CL_OFFSETOF(Object, cls) / sizeof(uint64_t);
        }

        NativeLayoutId native_layout;
        Shape *shape;
        OverflowSlots *overflow_storage;
        ClassObject *cls;

    protected:
        Value *inline_slot_base() { return reinterpret_cast<Value *>(&cls); }
        const Value *inline_slot_base() const
        {
            return reinterpret_cast<const Value *>(&cls);
        }

    private:
        void install_class(ClassObject *new_cls);
        void initialize_shape_for_class(ClassObject *class_object);
        void initialize_shape(Shape *initial_shape);
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

    static_assert(sizeof(Object) == 40);
    static_assert(std::is_trivially_destructible_v<Object>);
}  // namespace cl

#endif  // CL_OBJECT_H
