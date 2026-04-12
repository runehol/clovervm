#ifndef CL_OWNED_H
#define CL_OWNED_H

#include "refcount.h"
#include <type_traits>

namespace cl
{
    template <typename T> class TValue;
    enum class RefcountPolicy
    {
        Never,
        Maybe,
        Always
    };

    template <typename Handle> struct OwnedHandleTraits;
    template <typename Handle> class Member;

    template <> struct OwnedHandleTraits<Value>
    {
        static Value from_raw(Value value) { return value; }
        static Value to_raw(Value value) { return value; }
        static constexpr RefcountPolicy refcount_policy = RefcountPolicy::Maybe;
    };

    template <typename Handle> struct HandleRefOps
    {
        static constexpr RefcountPolicy refcount_policy =
            OwnedHandleTraits<Handle>::refcount_policy;

        static Value validate_raw(Value value)
        {
            (void)OwnedHandleTraits<Handle>::from_raw(value);
            return value;
        }

        static Value retain_ref(Value value)
        {
            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
                return value;
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                if(value == Value::None())
                {
                    return value;
                }
                return incref_refcounted_ptr(value);
            }
            else
            {
                return incref(value);
            }
        }

        static void release_ref(Value value)
        {
            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
                return;
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                if(value == Value::None())
                {
                    return;
                }
                decref_refcounted_ptr(value);
            }
            else
            {
                decref(value);
            }
        }
    };

    template <typename Handle> class Owned
    {
    public:
        Owned() : value_(Value::None()) {}
        explicit Owned(Value value)
            : value_(HandleRefOps<Handle>::retain_ref(
                  HandleRefOps<Handle>::validate_raw(value)))
        {
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Owned(H handle)
            : value_(HandleRefOps<Handle>::retain_ref(
                  OwnedHandleTraits<Handle>::to_raw(handle)))
        {
        }

        explicit Owned(const Member<Handle> &other)
            : value_(HandleRefOps<Handle>::retain_ref(Value(other)))
        {
        }

        Owned(const Owned &other)
            : value_(HandleRefOps<Handle>::retain_ref(other.value_))
        {
        }
        Owned(Owned &&other) noexcept : value_(other.release()) {}

        ~Owned() { HandleRefOps<Handle>::release_ref(value_); }

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
                HandleRefOps<Handle>::release_ref(value_);
                value_ = other.release();
            }
            return *this;
        }

        Owned &operator=(const Member<Handle> &other)
        {
            reset(Value(other));
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

        void reset(Value value)
        {
            value = HandleRefOps<Handle>::validate_raw(value);
            HandleRefOps<Handle>::retain_ref(value);
            HandleRefOps<Handle>::release_ref(value_);
            value_ = value;
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        void reset(H handle)
        {
            reset(OwnedHandleTraits<Handle>::to_raw(handle));
        }

        void clear()
        {
            HandleRefOps<Handle>::release_ref(value_);
            value_ = Value::None();
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

    template <typename Handle> class Member
    {
    public:
        Member() : value_(Value::None()) {}
        explicit Member(Value value)
            : value_(HandleRefOps<Handle>::retain_ref(
                  HandleRefOps<Handle>::validate_raw(value)))
        {
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Member(H handle)
            : value_(HandleRefOps<Handle>::retain_ref(
                  OwnedHandleTraits<Handle>::to_raw(handle)))
        {
        }

        explicit Member(const Owned<Handle> &other)
            : value_(HandleRefOps<Handle>::retain_ref(Value(other)))
        {
        }

        Member(const Member &other)
            : value_(HandleRefOps<Handle>::retain_ref(other.value_))
        {
        }
        Member(Member &&other) noexcept : value_(other.release()) {}

        Member &operator=(Value value)
        {
            reset(value);
            return *this;
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        Member &operator=(H handle)
        {
            reset(handle);
            return *this;
        }

        Member &operator=(const Member &other)
        {
            if(this != &other)
            {
                reset(other.value_);
            }
            return *this;
        }

        Member &operator=(Member &&other) noexcept
        {
            if(this != &other)
            {
                HandleRefOps<Handle>::release_ref(value_);
                value_ = other.release();
            }
            return *this;
        }

        Member &operator=(const Owned<Handle> &other)
        {
            reset(Value(other));
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

        void reset(Value value)
        {
            value = HandleRefOps<Handle>::validate_raw(value);
            HandleRefOps<Handle>::retain_ref(value);
            HandleRefOps<Handle>::release_ref(value_);
            value_ = value;
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        void reset(H handle)
        {
            reset(OwnedHandleTraits<Handle>::to_raw(handle));
        }

        void clear()
        {
            HandleRefOps<Handle>::release_ref(value_);
            value_ = Value::None();
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

    using OwnedValue = Owned<Value>;
    using MemberValue = Member<Value>;
    template <typename T> using MemberTValue = Member<TValue<T>>;

    static_assert(sizeof(OwnedValue) == sizeof(Value));
    static_assert(sizeof(MemberValue) == sizeof(Value));
    static_assert(std::is_trivially_destructible_v<MemberValue>);

}  // namespace cl

#endif  // CL_OWNED_H
