#ifndef CL_OWNED_H
#define CL_OWNED_H

#include "refcount.h"
#include <type_traits>

namespace cl
{

    template <typename Handle> struct OwnedHandleTraits;

    template <> struct OwnedHandleTraits<Value>
    {
        static Value from_raw(Value value) { return value; }
        static Value to_raw(Value value) { return value; }
    };

    template <typename Handle> class Owned
    {
    public:
        Owned() : value_(Value::None()) {}
        explicit Owned(Value value) : value_(incref(validate_raw(value))) {}

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Owned(H handle)
            : value_(incref(OwnedHandleTraits<Handle>::to_raw(handle)))
        {
        }

        Owned(const Owned &other) : value_(incref(other.value_)) {}
        Owned(Owned &&other) noexcept : value_(other.release()) {}

        ~Owned() { decref(value_); }

        Owned &operator=(Value value)
        {
            reset(value);
            return *this;
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        Owned &operator=(H handle)
        {
            reset(handle);
            return *this;
        }

        Owned &operator=(const Owned &other)
        {
            if(this != &other)
            {
                reset(other.value_);
            }
            return *this;
        }

        Owned &operator=(Owned &&other) noexcept
        {
            if(this != &other)
            {
                decref(value_);
                value_ = other.release();
            }
            return *this;
        }

        Handle get() const
        {
            return OwnedHandleTraits<Handle>::from_raw(value_);
        }
        operator Value() const { return value_; }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        operator Handle() const
        {
            return get();
        }

        template <typename T = Object> T *get_ptr() const
        {
            return value_.get_ptr<T>();
        }

        bool operator==(Value value) const { return value_ == value; }
        bool operator!=(Value value) const { return value_ != value; }

        void reset(Value value = Value::None())
        {
            value = validate_raw(value);
            incref(value);
            decref(value_);
            value_ = value;
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        void reset(H handle)
        {
            reset(OwnedHandleTraits<Handle>::to_raw(handle));
        }

        Value release()
        {
            Value released = value_;
            value_ = Value::None();
            return released;
        }

    private:
        static Value validate_raw(Value value)
        {
            (void)OwnedHandleTraits<Handle>::from_raw(value);
            return value;
        }

        Value value_;
    };

    using OwnedValue = Owned<Value>;

    static_assert(sizeof(OwnedValue) == sizeof(Value));

}  // namespace cl

#endif  // CL_OWNED_H
