#ifndef CL_OWNED_H
#define CL_OWNED_H

#include "refcount.h"
#include <type_traits>

namespace cl
{
    enum class RefcountPolicy
    {
        Never,
        Maybe,
        Always
    };

    template <typename Handle> struct OwnedHandleTraits;

    template <> struct OwnedHandleTraits<Value>
    {
        static Value from_raw(Value value) { return value; }
        static Value to_raw(Value value) { return value; }
        static constexpr RefcountPolicy refcount_policy = RefcountPolicy::Maybe;
    };

    template <typename Handle> class Owned
    {
    public:
        Owned() : value_(Value::None()) {}
        explicit Owned(Value value) : value_(retain(validate_raw(value))) {}

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Owned(H handle)
            : value_(retain(OwnedHandleTraits<Handle>::to_raw(handle)))
        {
        }

        Owned(const Owned &other) : value_(retain(other.value_)) {}
        Owned(Owned &&other) noexcept : value_(other.release()) {}

        ~Owned() { release_ref(value_); }

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
                release_ref(value_);
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
            retain(value);
            release_ref(value_);
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
        static constexpr RefcountPolicy refcount_policy =
            OwnedHandleTraits<Handle>::refcount_policy;

        static Value retain(Value value)
        {
            if(value == Value::None())
            {
                return value;
            }

            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
                return value;
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                return incref_refcounted_ptr(value);
            }
            else
            {
                return incref(value);
            }
        }

        static void release_ref(Value value)
        {
            if(value == Value::None())
            {
                return;
            }

            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
                return;
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                decref_refcounted_ptr(value);
            }
            else
            {
                decref(value);
            }
        }

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
