#ifndef CL_TYPED_VALUE_H
#define CL_TYPED_VALUE_H

#include "klass.h"
#include "object.h"
#include "owned_value.h"
#include "value.h"
#include <stdexcept>
#include <type_traits>

namespace cl
{
    struct String;
    extern Klass cl_string_klass;

    template <typename T> struct ValueTypeTraits
    {
        static_assert(std::is_base_of_v<Object, T>);

        static const Klass *expected_klass() { return &T::klass; }

        static bool is_instance(Value value)
        {
            return value.is_ptr() &&
                   value.get_ptr<Object>()->klass == expected_klass();
        }
    };

    template <> struct ValueTypeTraits<String>
    {
        static const Klass *expected_klass() { return &cl_string_klass; }

        static bool is_instance(Value value)
        {
            return value.is_ptr() &&
                   value.get_ptr<Object>()->klass == expected_klass();
        }
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
        static_assert(std::is_base_of_v<Object, T>);

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

        static TValue from_ptr(T *ptr)
        {
            return unsafe_unchecked(Value::from_oop(ptr));
        }

        Value raw() const { return value_; }
        operator Value() const { return value_; }

        T *get() const { return reinterpret_cast<T *>(value_.as.ptr); }
        T *operator->() const { return get(); }
        T &operator*() const { return *get(); }

        bool operator==(TValue other) const { return value_ == other.value_; }
        bool operator!=(TValue other) const { return value_ != other.value_; }

    private:
        TValue() = default;

        Value value_ = Value::None();
    };

    template <typename T> class OwnedTValue
    {
    public:
        static_assert(std::is_base_of_v<Object, T>);

        explicit OwnedTValue(Value value) : value_(value)
        {
            validate_typed_value<T>(value);
        }

        explicit OwnedTValue(TValue<T> value) : value_(value.raw()) {}

        OwnedTValue(const OwnedTValue &) = default;
        OwnedTValue(OwnedTValue &&) noexcept = default;
        OwnedTValue &operator=(const OwnedTValue &) = default;
        OwnedTValue &operator=(OwnedTValue &&) noexcept = default;

        OwnedTValue &operator=(Value value)
        {
            reset(value);
            return *this;
        }

        OwnedTValue &operator=(TValue<T> value)
        {
            reset(value);
            return *this;
        }

        TValue<T> get() const
        {
            return TValue<T>::unsafe_unchecked(value_.get());
        }

        Value raw() const { return value_.get(); }
        operator Value() const { return raw(); }
        operator TValue<T>() const { return get(); }

        T *get_ptr() const { return get().get(); }
        T *operator->() const { return get_ptr(); }
        T &operator*() const { return *get_ptr(); }

        void reset(Value value)
        {
            validate_typed_value<T>(value);
            value_.reset(value);
        }

        void reset(TValue<T> value) { value_.reset(value.raw()); }

        Value release() { return value_.release(); }

    private:
        OwnedValue value_;
    };

}  // namespace cl

#endif  // CL_TYPED_VALUE_H
