#ifndef CL_OWNED_H
#define CL_OWNED_H

#include "refcount.h"
#include <type_traits>

namespace cl
{
    template <typename Handle> struct HandleTraits;
    template <typename Handle> class Member;

    template <> struct HandleTraits<Value>
    {
        static Value from_value(Value value) { return value; }
        static Value from_value_unchecked(Value value) { return value; }
        static Value to_value(Value value) { return value; }
        static Value none() { return Value::None(); }
        using extracted_type = void;
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
            : handle_(
                  HandleTraits<Handle>::retain_ref(static_cast<Handle>(other)))
        {
        }

        Owned(const Owned &other)
            : handle_(
                  HandleTraits<Handle>::retain_ref(static_cast<Handle>(other)))
        {
        }

        ~Owned() { HandleTraits<Handle>::release_ref(handle_); }

        Owned &operator=(Value value)
        {
            assign(HandleTraits<Handle>::from_value(value));
            return *this;
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        Owned &operator=(H handle)
        {
            assign(handle);
            return *this;
        }

        Owned &operator=(const Owned &other)
        {
            if(this != &other)
            {
                assign(other.handle_);
            }
            return *this;
        }

        Owned &operator=(const Member<Handle> &other)
        {
            assign(static_cast<Handle>(other));
            return *this;
        }

        template <typename H = Handle,
                  typename Extracted = typename HandleTraits<H>::extracted_type,
                  typename = std::enable_if_t<!std::is_void_v<Extracted>>>
        Extracted extract() const
        {
            return HandleTraits<Handle>::extract(handle_);
        }

        Handle value() const { return handle_; }
        Value raw_value() const
        {
            return HandleTraits<Handle>::to_value(handle_);
        }
        operator Handle() const { return handle_; }

        bool operator==(Value value) const { return raw_value() == value; }
        bool operator!=(Value value) const { return raw_value() != value; }

    private:
        void assign(Handle handle)
        {
            HandleTraits<Handle>::retain_ref(handle);
            HandleTraits<Handle>::release_ref(handle_);
            handle_ = handle;
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
            : handle_(
                  HandleTraits<Handle>::retain_ref(static_cast<Handle>(other)))
        {
        }

        Member(const Member &other)
            : handle_(
                  HandleTraits<Handle>::retain_ref(static_cast<Handle>(other)))
        {
        }

        Member &operator=(Value value)
        {
            assign(HandleTraits<Handle>::from_value(value));
            return *this;
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        Member &operator=(H handle)
        {
            assign(handle);
            return *this;
        }

        Member &operator=(const Member &other)
        {
            if(this != &other)
            {
                assign(other.handle_);
            }
            return *this;
        }

        Member &operator=(const Owned<Handle> &other)
        {
            assign(static_cast<Handle>(other));
            return *this;
        }

        template <typename H = Handle,
                  typename Extracted = typename HandleTraits<H>::extracted_type,
                  typename = std::enable_if_t<!std::is_void_v<Extracted>>>
        Extracted extract() const
        {
            return HandleTraits<Handle>::extract(handle_);
        }

        Handle value() const { return handle_; }
        Value raw_value() const
        {
            return HandleTraits<Handle>::to_value(handle_);
        }
        operator Handle() const { return handle_; }

        bool operator==(Value value) const { return raw_value() == value; }
        bool operator!=(Value value) const { return raw_value() != value; }

        void release_ref() { HandleTraits<Handle>::release_ref(handle_); }

    private:
        void assign(Handle handle)
        {
            HandleTraits<Handle>::retain_ref(handle);
            HandleTraits<Handle>::release_ref(handle_);
            handle_ = handle;
        }

        Handle handle_;
    };

}  // namespace cl

#endif  // CL_OWNED_H
