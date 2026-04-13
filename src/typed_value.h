#ifndef CL_TYPED_VALUE_H
#define CL_TYPED_VALUE_H

#include "object.h"
#include "value.h"
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
    extern Klass cl_string_klass;

    template <typename T, typename Enable = void> struct ValueTypeTraits;
    template <typename T> struct ExactKlassProvider
    {
        static const Klass *expected_klass() { return &T::klass; }
    };

    struct StringKlassProvider
    {
        static const Klass *expected_klass() { return &cl_string_klass; }
    };

    template <typename T, typename KlassProvider, RefcountPolicy Policy>
    struct PointerBackedValueTypeTraits
    {
        static const Klass *expected_klass()
        {
            return KlassProvider::expected_klass();
        }

        static bool is_instance(Value value)
        {
            return value.is_ptr() &&
                   value.get_ptr<Object>()->klass == expected_klass();
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
        : PointerBackedValueTypeTraits<T, ExactKlassProvider<T>,
                                       RefcountPolicy::Always>
    {
    };

    template <>
    struct ValueTypeTraits<String>
        : PointerBackedValueTypeTraits<String, StringKlassProvider,
                                       RefcountPolicy::Maybe>
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
        explicit TValue(Value value) : value_(value)
        {
            validate_typed_value<T>(value_);
        }

        static TValue unsafe_unchecked(Value value)
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
            return unsafe_unchecked(Value::from_oop(ptr));
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
