#ifndef CL_VALUE_H
#define CL_VALUE_H

#include "object_model/object.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <type_traits>

namespace cl
{
    void add_to_active_zero_count_table_if_needed(HeapObject *obj);

/*
  A cl_value is a 64-bit generic cell to hold any value. It holds some of them
  inline, and some of them indirect.

  The structure is as follows, with a 5 bit tag in the lowest bits
  XXXXXXXXX....XXXXTTTTT

  The tag is divided as follows:
  Bit 4 (16): Refcounted pointer
  Bit 3 (8): Interned pointer
  Bit 2 (4): Boolean. Also sets bit 5 for truthiness, for easy conversion and
  comparison to int Bit 1-0: Other special values

  The tags then are as follows:
  00000: small integer, with the value is stored as signed two complement in the
  upper 59 bits. 00011: special truthy value. Only one known: 0x23: True 00010:
  special falsy value. Two known: 0x22: False, 0x42: None 01000: interned
  pointer 10000: refcounted pointer



*/
#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif
#ifndef ALWAYSINLINE
#define ALWAYSINLINE inline __attribute__((always_inline))
#endif
#ifndef INLINE
#define INLINE inline
#endif
#ifndef NOINLINE
#define NOINLINE __attribute__((noinline))
#endif
#ifndef MUSTTAIL
#define MUSTTAIL __attribute__((musttail))
#endif

    static constexpr uint64_t value_tag_bits = 5;
    static constexpr uint64_t value_tag_mask = 0x1f;
    static constexpr uint64_t value_ptr_granularity = value_tag_mask + 1;
    static constexpr uint64_t value_refcounted_ptr_tag = 0x10;
    static constexpr uint64_t value_interned_ptr_tag = 0x08;
    static constexpr uint64_t value_special_mask = 0x07;
    static constexpr uint64_t value_boolean_tag = 0x04;
    static constexpr uint64_t value_none = 0x01;
    static constexpr uint64_t value_not_present = 0x02;
    static constexpr uint64_t value_exception = 0x03;
    static constexpr uint64_t value_not_implemented = 0x21;
    static constexpr uint64_t value_ellipsis = 0x41;
    static constexpr uint64_t value_truthy_mask = 0xffffffffffffffe0ull;
    static constexpr uint64_t value_ptr_mask =
        value_refcounted_ptr_tag | value_interned_ptr_tag;
    static constexpr uint64_t value_not_smi_mask = value_tag_mask;
    static constexpr uint64_t value_not_smi_or_boolean_mask =
        value_tag_mask & ~value_boolean_tag;
    static constexpr uint64_t value_boolean_to_integer_mask =
        ~value_boolean_tag;
    static constexpr int64_t value_smi_min =
        -(int64_t{1} << (64 - value_tag_bits - 1));
    static constexpr int64_t value_smi_max =
        (int64_t{1} << (64 - value_tag_bits - 1)) - 1;

    static_assert((value_not_implemented & value_ptr_mask) == 0);
    static_assert((value_ellipsis & value_ptr_mask) == 0);
    static_assert((value_not_implemented & value_truthy_mask) != 0);
    static_assert((value_ellipsis & value_truthy_mask) != 0);
    static_assert((value_not_implemented >> value_tag_bits) != 0);
    static_assert((value_ellipsis >> value_tag_bits) != 0);

    enum class ValueStorageClass
    {
        Inline,
        InternedPtr,
        RefcountedPtr
    };

    enum class RefcountPolicy
    {
        Never,
        Maybe,
        Always
    };

    class Value
    {
    public:
        using semantic_type = Value;

        static inline Value from_value_unchecked(Value value) { return value; }

        static inline Value from_smi(int64_t v)
        {
            Value val;
            val.as.integer = v << value_tag_bits;
            return val;
        }

        static inline Value from_oop(Object *obj)
        {
            Value val;
            val.as.ptr = obj;
            return val;
        }

        static inline Value None()
        {
            Value val;
            val.as.integer = value_none;
            return val;
        }

        static inline Value NotImplemented()
        {
            Value val;
            val.as.integer = value_not_implemented;
            return val;
        }

        static inline Value Ellipsis()
        {
            Value val;
            val.as.integer = value_ellipsis;
            return val;
        }

        static inline Value False()
        {
            Value val;
            val.as.integer = value_boolean_tag;
            return val;
        }

        static inline Value True()
        {
            Value val;
            val.as.integer = value_boolean_tag | (1 << value_tag_bits);
            return val;
        }

        static inline Value exception_marker()
        {
            Value val;
            val.as.integer = value_exception;  // special value to return if an
                                               // exception has been thrown.
            return val;
        }

        bool is_exception_marker() const
        {
            return as.integer == value_exception;
        }

        static inline Value not_present()
        {
            Value val;
            val.as.integer = value_not_present;
            return val;
        }

        bool is_not_present() const
        {
            uint32_t v = as.integer;
            return v == value_not_present;
        }

        bool is_vm_sentinel() const
        {
            return is_exception_marker() || is_not_present();
        }

        void assert_not_exception_marker() const
        {
            assert(!is_exception_marker());
        }

        void assert_not_vm_sentinel() const { assert(!is_vm_sentinel()); }

        union
        {
            long long integer;
            HeapObject *ptr;
        } as;

        bool operator==(Value o) const { return as.integer == o.as.integer; }
        bool operator!=(Value o) const { return as.integer != o.as.integer; }

        Value raw_value() const { return *this; }

        bool is_smi() const { return (as.integer & value_tag_mask) == 0; }

        bool is_integer() const { return is_smi(); }

        bool is_bool() const
        {
            return (as.integer & value_tag_mask) == value_boolean_tag;
        }

        bool is_none() const { return as.integer == value_none; }

        bool is_not_implemented_singleton() const
        {
            return as.integer == value_not_implemented;
        }

        bool is_ellipsis_singleton() const
        {
            return as.integer == value_ellipsis;
        }

        ValueStorageClass storage_class() const
        {
            switch(as.integer & value_ptr_mask)
            {
                case 0:
                    return ValueStorageClass::Inline;
                case value_interned_ptr_tag:
                    return ValueStorageClass::InternedPtr;
                case value_refcounted_ptr_tag:
                    return ValueStorageClass::RefcountedPtr;
                default:
                    __builtin_unreachable();
            }
        }

        bool is_inline() const
        {
            return storage_class() == ValueStorageClass::Inline;
        }

        bool is_ptr() const { return (as.integer & value_ptr_mask) != 0; }

        bool is_interned_ptr() const
        {
            return storage_class() == ValueStorageClass::InternedPtr;
        }

        bool is_refcounted_ptr() const
        {
            return storage_class() == ValueStorageClass::RefcountedPtr;
        }

        int64_t get_smi() const
        {
            assert(is_smi());
            return as.integer >> value_tag_bits;
        }

        bool is_smi8() const
        {
            if(!is_smi())
                return false;

            int64_t v = get_smi();
            return v >= -128 && v <= 127;
        }

        template <typename T = Object,
                  typename = std::enable_if_t<std::is_base_of_v<Object, T>>>
        T *get_ptr() const
        {
            assert(is_ptr());
            return reinterpret_cast<T *>(as.ptr);
        }

        bool is_truthy() const
        {
            assert(!is_ptr());
            return (as.integer & value_truthy_mask) != 0;
        }
        bool is_falsy() const
        {
            assert(!is_ptr());
            return (as.integer & value_truthy_mask) == 0;
        }
    };

    template <typename T> class HeapPtr
    {
    public:
        HeapPtr() : ptr_(nullptr) {}
        HeapPtr(std::nullptr_t) : ptr_(nullptr) {}
        HeapPtr(T *ptr) : ptr_(ptr) {}

        T *get() const { return ptr_; }
        T *extract() const { return ptr_; }

        explicit operator bool() const { return ptr_ != nullptr; }
        bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
        bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    private:
        T *ptr_;
    };

    static_assert(sizeof(HeapPtr<HeapObject>) == sizeof(HeapObject *));

    template <typename T> bool can_convert_to(Value value)
    {
        return value.is_ptr() && can_convert_to<T>(value.get_ptr<Object>());
    }

    template <typename T> T *try_convert_to(Value value)
    {
        return value.is_ptr() ? try_convert_to<T>(value.get_ptr<Object>())
                              : nullptr;
    }

    template <typename T> T *assume_convert_to(Value value)
    {
        assert(can_convert_to<T>(value));
        return static_cast<T *>(value.get_ptr<Object>());
    }

}  // namespace cl

#ifndef CL_OVERFLOW_SLOTS_H
#include "object_model/overflow_slots.h"

namespace cl
{
    ALWAYSINLINE Value
    Object::read_storage_location(StorageLocation location) const
    {
        if(likely(location.kind == StorageKind::Inline))
        {
            return inline_slot_base()[location.physical_idx];
        }
        if(unlikely(location.kind == StorageKind::Overflow))
        {
            OverflowSlots *overflow_slots = get_overflow_slots();
            assert(overflow_slots != nullptr);
            assert(uint32_t(location.physical_idx) <
                   overflow_slots->get_size());
            return overflow_slots->get(location.physical_idx);
        }
        __builtin_unreachable();
    }

    ALWAYSINLINE HeapObject *
    Object::write_existing_storage_location_returning_zero_ref(
        StorageLocation location, Value value)
    {
        Value *slots = nullptr;
        switch(location.kind)
        {
            case StorageKind::Inline:
                slots = inline_slot_base();
                break;
            case StorageKind::Overflow:
                {
                    OverflowSlots *overflow_slots = get_overflow_slots();
                    assert(overflow_slots != nullptr);
                    assert(uint32_t(location.physical_idx) <
                           overflow_slots->get_size());
                    slots = overflow_slots->slot_value_base();
                    break;
                }
        }

        Value old_value = slots[location.physical_idx];
        if(value.is_refcounted_ptr())
        {
            ++value.as.ptr->refcount;
        }
        slots[location.physical_idx] = value;
        if(old_value.is_refcounted_ptr() && --old_value.as.ptr->refcount == 0)
        {
            return old_value.as.ptr;
        }
        return nullptr;
    }

    ALWAYSINLINE void
    Object::write_existing_storage_location(StorageLocation location,
                                            Value value)
    {
        HeapObject *zct_object =
            write_existing_storage_location_returning_zero_ref(location, value);
        if(unlikely(zct_object != nullptr))
        {
            add_to_active_zero_count_table_if_needed(zct_object);
        }
    }
}  // namespace cl
#endif  // CL_OVERFLOW_SLOTS_H

#endif  // CL_VALUE_H
