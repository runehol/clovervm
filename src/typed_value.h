#ifndef CL_TYPED_VALUE_H
#define CL_TYPED_VALUE_H

#include "object.h"
#include "value.h"
#include <cassert>
#include <stdexcept>
#include <type_traits>

namespace cl
{
    struct String;
    struct SMI
    {
    };
    struct CLInt
    {
    };

    template <typename T, typename Enable = void> struct ValueTypeTraits;
    template <typename T> struct ExactNativeLayoutProvider
    {
        static constexpr NativeLayoutId expected_native_layout_id()
        {
            return T::native_layout_id;
        }
    };

    template <typename T, typename NativeLayoutProvider, RefcountPolicy Policy>
    struct PointerBackedValueTypeTraits
    {
        static constexpr NativeLayoutId expected_native_layout_id()
        {
            return NativeLayoutProvider::expected_native_layout_id();
        }

        static bool is_instance(Value value)
        {
            return value.is_ptr() &&
                   value.get_ptr<Object>()->native_layout_id() ==
                       expected_native_layout_id();
        }

        using get_type = T *;
        static constexpr RefcountPolicy refcount_policy = Policy;

        static get_type get_unchecked(Value value)
        {
            return reinterpret_cast<T *>(value.as.ptr);
        }
    };

    template <typename T>
    struct ValueTypeTraits<T, std::enable_if_t<std::is_base_of_v<Object, T>>>
        : PointerBackedValueTypeTraits<T, ExactNativeLayoutProvider<T>,
                                       RefcountPolicy::Always>
    {
    };

    template <>
    struct ValueTypeTraits<String>
        : PointerBackedValueTypeTraits<
              String, ExactNativeLayoutProvider<String>, RefcountPolicy::Maybe>
    {
    };

    template <> struct ValueTypeTraits<SMI>
    {
        static bool is_instance(Value value) { return value.is_smi(); }

        using get_type = int64_t;
        static constexpr RefcountPolicy refcount_policy = RefcountPolicy::Never;

        static get_type get_unchecked(Value value) { return value.get_smi(); }
    };

    template <> struct ValueTypeTraits<CLInt>
    {
        static bool is_instance(Value value) { return value.is_integer(); }

        using get_type = void;
        static constexpr RefcountPolicy refcount_policy = RefcountPolicy::Maybe;
    };

    template <typename T> inline void validate_typed_value(Value value)
    {
        if(ValueTypeTraits<T>::is_instance(value))
        {
            return;
        }

        throw std::runtime_error("TypeError: invalid typed value construction");
    }

    template <typename T> class TValue
    {
    public:
        static TValue from_value_checked(Value value)
        {
            validate_typed_value<T>(value);
            return from_value_unchecked(value);
        }

        static TValue from_value_assumed(Value value)
        {
            assert(ValueTypeTraits<T>::is_instance(value));
            return from_value_unchecked(value);
        }

        static TValue from_value_unchecked(Value value)
        {
            TValue typed_value;
            typed_value.value_ = value;
            return typed_value;
        }

        template <typename U = T,
                  typename GetType = typename ValueTypeTraits<U>::get_type,
                  typename = std::enable_if_t<std::is_pointer_v<GetType>>>
        static TValue from_oop(std::remove_pointer_t<GetType> *ptr)
        {
            return from_value_unchecked(Value::from_oop(ptr));
        }

        template <typename U = T,
                  typename GetType = typename ValueTypeTraits<U>::get_type,
                  typename = std::enable_if_t<!std::is_void_v<GetType>>>
        GetType extract() const
        {
            return ValueTypeTraits<U>::get_unchecked(value_);
        }

        Value as_value() const { return value_; }
        operator Value() const { return value_; }

        bool operator==(TValue other) const { return value_ == other.value_; }
        bool operator!=(TValue other) const { return value_ != other.value_; }

    private:
        TValue() = default;

        Value value_ = Value::None();
    };

}  // namespace cl

#endif  // CL_TYPED_VALUE_H
