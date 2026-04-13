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

    template <typename Handle> struct HandleTraits;
    template <typename Handle> class Member;

    template <> struct HandleTraits<Value>
    {
        static Value from_value(Value value) { return value; }
        static Value from_value_unchecked(Value value) { return value; }
        static Value none() { return Value::None(); }
        static constexpr RefcountPolicy refcount_policy = RefcountPolicy::Maybe;

        static Value retain_ref(Value value) { return incref(value); }

        static void release_ref(Value value) { decref(value); }
    };

    template <typename Handle> class Owned
    {
    public:
        Owned() : handle_(HandleTraits<Handle>::none()) {}
        explicit Owned(Value value)
            : handle_(HandleTraits<Handle>::retain_ref(
                  HandleTraits<Handle>::from_value(value)))
        {
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Owned(H handle)
            : handle_(HandleTraits<Handle>::retain_ref(handle))
        {
        }

        explicit Owned(const Member<Handle> &other)
            : handle_(HandleTraits<Handle>::retain_ref(other))
        {
        }

        Owned(const Owned &other)
            : handle_(HandleTraits<Handle>::retain_ref(other))
        {
        }
        Owned(Owned &&other) noexcept : handle_(other.release()) {}

        ~Owned() { HandleTraits<Handle>::release_ref(handle_); }

        Owned &operator=(Value value)
        {
            reset(HandleTraits<Handle>::from_value(value));
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
                HandleTraits<Handle>::release_ref(handle_);
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

        Value as_value() const { return handle_to_value(handle_); }
        operator Value() const { return handle_to_value(handle_); }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        operator Handle() const
        {
            return handle_;
        }

        template <typename T = Object> T *get_ptr() const
        {
            return handle_to_value(handle_).template get_ptr<T>();
        }

        bool operator==(Value value) const { return as_value() == value; }
        bool operator!=(Value value) const { return as_value() != value; }

        void reset(Handle handle)
        {
            HandleTraits<Handle>::retain_ref(handle);
            HandleTraits<Handle>::release_ref(handle_);
            handle_ = handle;
        }

        void clear()
        {
            HandleTraits<Handle>::release_ref(handle_);
            handle_ = HandleTraits<Handle>::none();
        }

        Handle release()
        {
            Handle released = handle_;
            handle_ = HandleTraits<Handle>::none();
            return released;
        }

    private:
        static Value handle_to_value(Handle handle)
        {
            if constexpr(std::is_same_v<Handle, Value>)
            {
                return handle;
            }
            else
            {
                return HandleTraits<Handle>::to_value(handle);
            }
        }

        Handle handle_;
    };

    template <typename Handle> class Member
    {
    public:
        Member() : handle_(HandleTraits<Handle>::none()) {}
        explicit Member(Value value)
            : handle_(HandleTraits<Handle>::retain_ref(
                  HandleTraits<Handle>::from_value(value)))
        {
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        explicit Member(H handle)
            : handle_(HandleTraits<Handle>::retain_ref(handle))
        {
        }

        explicit Member(const Owned<Handle> &other)
            : handle_(HandleTraits<Handle>::retain_ref(other))
        {
        }

        Member(const Member &other)
            : handle_(HandleTraits<Handle>::retain_ref(other))
        {
        }
        Member(Member &&other) noexcept : handle_(other.release()) {}

        Member &operator=(Value value)
        {
            reset(HandleTraits<Handle>::from_value(value));
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
                HandleTraits<Handle>::release_ref(handle_);
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

        Value as_value() const { return handle_to_value(handle_); }
        operator Value() const { return handle_to_value(handle_); }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        operator Handle() const
        {
            return handle_;
        }

        template <typename T = Object> T *get_ptr() const
        {
            return handle_to_value(handle_).template get_ptr<T>();
        }

        bool operator==(Value value) const { return as_value() == value; }
        bool operator!=(Value value) const { return as_value() != value; }

        void reset(Handle handle)
        {
            HandleTraits<Handle>::retain_ref(handle);
            HandleTraits<Handle>::release_ref(handle_);
            handle_ = handle;
        }

        void clear()
        {
            HandleTraits<Handle>::release_ref(handle_);
            handle_ = HandleTraits<Handle>::none();
        }

        Handle release()
        {
            Handle released = handle_;
            handle_ = HandleTraits<Handle>::none();
            return released;
        }

    private:
        static Value handle_to_value(Handle handle)
        {
            if constexpr(std::is_same_v<Handle, Value>)
            {
                return handle;
            }
            else
            {
                return HandleTraits<Handle>::to_value(handle);
            }
        }

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
