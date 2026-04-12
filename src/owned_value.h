#ifndef CL_OWNED_VALUE_H
#define CL_OWNED_VALUE_H

#include "refcount.h"

namespace cl
{

    class OwnedValue
    {
    public:
        OwnedValue() : value_(Value::None()) {}
        explicit OwnedValue(Value value) : value_(incref(value)) {}

        OwnedValue(const OwnedValue &other) : value_(incref(other.value_)) {}

        OwnedValue(OwnedValue &&other) noexcept : value_(other.release()) {}

        ~OwnedValue() { decref(value_); }

        OwnedValue &operator=(Value value)
        {
            reset(value);
            return *this;
        }

        OwnedValue &operator=(const OwnedValue &other)
        {
            if(this != &other)
            {
                reset(other.value_);
            }
            return *this;
        }

        OwnedValue &operator=(OwnedValue &&other) noexcept
        {
            if(this != &other)
            {
                decref(value_);
                value_ = other.release();
            }
            return *this;
        }

        Value get() const { return value_; }
        operator Value() const { return value_; }

        template <typename T = Object> T *get_ptr() const
        {
            return value_.get_ptr<T>();
        }

        bool operator==(Value value) const { return value_ == value; }
        bool operator!=(Value value) const { return value_ != value; }

        void reset(Value value = Value::None())
        {
            incref(value);
            decref(value_);
            value_ = value;
        }

        Value release()
        {
            Value released = value_;
            value_ = Value::None();
            return released;
        }

    private:
        Value value_;
    };

    static_assert(sizeof(OwnedValue) == sizeof(Value));

}  // namespace cl

#endif  // CL_OWNED_VALUE_H
