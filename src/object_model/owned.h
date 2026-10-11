#ifndef CL_OWNED_H
#define CL_OWNED_H

#include "object_model/heap_ptr.h"
#include "object_model/refcount.h"
#include "object_model/typed_value.h"
#include <type_traits>

namespace cl
{
    template <typename T> struct HandleRefcountTraits
    {
        using semantic_type = typename T::semantic_type;

        static constexpr bool is_fully_inline =
            std::is_same_v<semantic_type, SMI> ||
            std::is_same_v<semantic_type, Bool> ||
            std::is_same_v<semantic_type, None>;
        static constexpr bool may_need_refcounting = !is_fully_inline;
    };

    template <typename T> static inline void incref_handle(T value)
    {
        if constexpr(HandleRefcountTraits<T>::may_need_refcounting)
        {
            incref(value.raw_value());
        }
    }

    template <typename T> static inline void decref_handle(T value)
    {
        if constexpr(HandleRefcountTraits<T>::may_need_refcounting)
        {
            decref(value.raw_value());
        }
    }

    template <typename T> class Member;

    template <typename T> class Owned
    {
    public:
        using semantic_type = typename T::semantic_type;

        Owned()
        requires(std::is_default_constructible_v<T> &&
                 !std::is_same_v<T, Value>)
            : value_()
        {
            incref_handle(value_);
        }
        explicit Owned(T value) : value_(value) { incref_handle(value_); }
        explicit Owned(const Member<T> &other) : value_(other.value())
        {
            incref_handle(value_);
        }

        Owned(const Owned &other) : value_(other.value_)
        {
            incref_handle(value_);
        }

        ~Owned() { decref_handle(value_); }

        Owned &operator=(T value)
        {
            assign(value);
            return *this;
        }

        Owned &operator=(const Owned &other)
        {
            if(this != &other)
            {
                assign(other.value_);
            }
            return *this;
        }

        Owned &operator=(const Member<T> &other)
        {
            assign(other.value());
            return *this;
        }

        T value() const { return value_; }
        T operator*() const { return value(); }
        operator T() const { return value(); }
        Value raw_value() const { return value_.raw_value(); }

        decltype(auto) extract() const
        requires requires(const T &value) { value.extract(); }
        {
            return value_.extract();
        }

    private:
        void assign(T value)
        {
            incref_handle(value);
            decref_handle(value_);
            value_ = value;
        }

        T value_;
    };

    template <typename T> class Member
    {
    public:
        using semantic_type = typename T::semantic_type;

        Member()
        requires(std::is_default_constructible_v<T> &&
                 !std::is_same_v<T, Value>)
            : value_()
        {
            incref_handle(value_);
        }
        explicit Member(T value) : value_(value) { incref_handle(value_); }
        explicit Member(const Owned<T> &other) : value_(other.value())
        {
            incref_handle(value_);
        }

        Member(const Member &other) : value_(other.value_)
        {
            incref_handle(value_);
        }

        Member &operator=(T value)
        {
            assign(value);
            return *this;
        }

        Member &operator=(const Member &other)
        {
            if(this != &other)
            {
                assign(other.value_);
            }
            return *this;
        }

        Member &operator=(const Owned<T> &other)
        {
            assign(other.value());
            return *this;
        }

        T value() const { return value_; }
        T operator*() const { return value(); }
        operator T() const { return value(); }
        Value raw_value() const { return value_.raw_value(); }

        decltype(auto) extract() const
        requires requires(const T &value) { value.extract(); }
        {
            return value_.extract();
        }

        // For custom heap-object dealloc paths. Leaves the member value
        // unchanged; the containing object must not use the member afterward.
        void release_ref() { decref_handle(value_); }

    private:
        void assign(T value)
        {
            incref_handle(value);
            decref_handle(value_);
            value_ = value;
        }

        T value_;
    };

}  // namespace cl

#endif  // CL_OWNED_H
