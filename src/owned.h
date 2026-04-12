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
        static Value from_raw_unchecked(Value value) { return value; }
        static Value to_raw(Value value) { return value; }
        static constexpr RefcountPolicy refcount_policy = RefcountPolicy::Maybe;
    };

    template <typename Handle> struct HandleRefOps
    {
        static constexpr RefcountPolicy refcount_policy =
            OwnedHandleTraits<Handle>::refcount_policy;

        static Handle validate_raw(Value value)
        {
            return OwnedHandleTraits<Handle>::from_raw(value);
        }

        static Handle retain_ref(Handle handle)
        {
            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                if(Value(handle) != Value::None())
                {
                    incref_refcounted_ptr(handle);
                }
            }
            else
            {
                incref(handle);
            }
            return handle;
        }

        static void release_ref(Handle handle)
        {
            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                if(Value(handle) != Value::None())
                {
                    decref_refcounted_ptr(handle);
                }
            }
            else
            {
                decref(handle);
            }
        }
    };

    template <typename Handle> class Owned
    {
    public:
        Owned()
            : handle_(
                  OwnedHandleTraits<Handle>::from_raw_unchecked(Value::None()))
        {
        }
        explicit Owned(Value value)
            : handle_(HandleRefOps<Handle>::retain_ref(
                  HandleRefOps<Handle>::validate_raw(value)))
        {
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Owned(H handle)
            : handle_(HandleRefOps<Handle>::retain_ref(handle))
        {
        }

        explicit Owned(const Member<Handle> &other)
            : handle_(HandleRefOps<Handle>::retain_ref(other))
        {
        }

        Owned(const Owned &other)
            : handle_(HandleRefOps<Handle>::retain_ref(other))
        {
        }
        Owned(Owned &&other) noexcept : handle_(other.release()) {}

        ~Owned() { HandleRefOps<Handle>::release_ref(handle_); }

        Owned &operator=(Value value)
        {
            reset(HandleRefOps<Handle>::validate_raw(value));
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
                reset(other.handle_);
            }
            return *this;
        }

        Owned &operator=(Owned &&other) noexcept
        {
            if(this != &other)
            {
                HandleRefOps<Handle>::release_ref(handle_);
                handle_ = other.release();
            }
            return *this;
        }

        Owned &operator=(const Member<Handle> &other)
        {
            reset(other);
            return *this;
        }

        template <typename H = Handle,
                  typename Inner = decltype(std::declval<H>().get()),
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        Inner get() const
        {
            return handle_.get();
        }

        Value as_value() const
        {
            return OwnedHandleTraits<Handle>::to_raw(handle_);
        }
        operator Value() const
        {
            return OwnedHandleTraits<Handle>::to_raw(handle_);
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        operator Handle() const
        {
            return handle_;
        }

        template <typename T = Object> T *get_ptr() const
        {
            return OwnedHandleTraits<Handle>::to_raw(handle_)
                .template get_ptr<T>();
        }

        bool operator==(Value value) const { return handle_ == value; }
        bool operator!=(Value value) const { return handle_ != value; }

        void reset(Handle handle)
        {
            HandleRefOps<Handle>::retain_ref(handle);
            HandleRefOps<Handle>::release_ref(handle_);
            handle_ = handle;
        }

        void clear()
        {
            HandleRefOps<Handle>::release_ref(handle_);
            handle_ =
                OwnedHandleTraits<Handle>::from_raw_unchecked(Value::None());
        }

        Handle release()
        {
            Handle released = handle_;
            handle_ =
                OwnedHandleTraits<Handle>::from_raw_unchecked(Value::None());
            return released;
        }

    private:
        Handle handle_;
    };

    template <typename Handle> class Member
    {
    public:
        Member()
            : handle_(
                  OwnedHandleTraits<Handle>::from_raw_unchecked(Value::None()))
        {
        }
        explicit Member(Value value)
            : handle_(HandleRefOps<Handle>::retain_ref(
                  HandleRefOps<Handle>::validate_raw(value)))
        {
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Member(H handle)
            : handle_(HandleRefOps<Handle>::retain_ref(handle))
        {
        }

        explicit Member(const Owned<Handle> &other)
            : handle_(HandleRefOps<Handle>::retain_ref(other))
        {
        }

        Member(const Member &other)
            : handle_(HandleRefOps<Handle>::retain_ref(other))
        {
        }
        Member(Member &&other) noexcept : handle_(other.release()) {}

        Member &operator=(Value value)
        {
            reset(HandleRefOps<Handle>::validate_raw(value));
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
                reset(other.handle_);
            }
            return *this;
        }

        Member &operator=(Member &&other) noexcept
        {
            if(this != &other)
            {
                HandleRefOps<Handle>::release_ref(handle_);
                handle_ = other.release();
            }
            return *this;
        }

        Member &operator=(const Owned<Handle> &other)
        {
            reset(other);
            return *this;
        }

        template <typename H = Handle,
                  typename Inner = decltype(std::declval<H>().get()),
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        Inner get() const
        {
            return handle_.get();
        }

        Value as_value() const
        {
            return OwnedHandleTraits<Handle>::to_raw(handle_);
        }
        operator Value() const
        {
            return OwnedHandleTraits<Handle>::to_raw(handle_);
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        operator Handle() const
        {
            return handle_;
        }

        template <typename T = Object> T *get_ptr() const
        {
            return OwnedHandleTraits<Handle>::to_raw(handle_)
                .template get_ptr<T>();
        }

        bool operator==(Value value) const { return Value(handle_) == value; }
        bool operator!=(Value value) const { return Value(handle_) != value; }

        void reset(Handle handle)
        {
            HandleRefOps<Handle>::retain_ref(handle);
            HandleRefOps<Handle>::release_ref(handle_);
            handle_ = handle;
        }

        void clear()
        {
            HandleRefOps<Handle>::release_ref(handle_);
            handle_ =
                OwnedHandleTraits<Handle>::from_raw_unchecked(Value::None());
        }

        Handle release()
        {
            Handle released = handle_;
            handle_ =
                OwnedHandleTraits<Handle>::from_raw_unchecked(Value::None());
            return released;
        }

    private:
        Handle handle_;
    };

    using OwnedValue = Owned<Value>;
    using MemberValue = Member<Value>;
    template <typename T> using MemberTValue = Member<TValue<T>>;

    static_assert(sizeof(OwnedValue) == sizeof(Value));
    static_assert(sizeof(MemberValue) == sizeof(Value));
    static_assert(std::is_trivially_destructible_v<MemberValue>);

}  // namespace cl

#endif  // CL_OWNED_H
