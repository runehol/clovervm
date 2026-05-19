#ifndef CL_OWNED2_H
#define CL_OWNED2_H

#include "refcount.h"
#include "value_state.h"
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

    template <typename T> static inline void incref_value_state(T value)
    {
        if constexpr(HandleRefcountTraits<T>::may_need_refcounting)
        {
            incref(value.raw_value());
        }
    }

    template <typename T> static inline void decref_value_state(T value)
    {
        if constexpr(HandleRefcountTraits<T>::may_need_refcounting)
        {
            decref(value.raw_value());
        }
    }

    template <typename T> class Member2;

    template <typename T> class Owned2
    {
    public:
        using semantic_type = typename T::semantic_type;

        Owned2() = delete;
        explicit Owned2(T value) : value_(value) { incref_value_state(value_); }
        explicit Owned2(const Member2<T> &other) : value_(other.value())
        {
            incref_value_state(value_);
        }

        Owned2(const Owned2 &other) : value_(other.value_)
        {
            incref_value_state(value_);
        }

        ~Owned2() { decref_value_state(value_); }

        Owned2 &operator=(T value)
        {
            reset(value);
            return *this;
        }

        Owned2 &operator=(const Owned2 &other)
        {
            if(this != &other)
            {
                reset(other.value_);
            }
            return *this;
        }

        Owned2 &operator=(const Member2<T> &other)
        {
            reset(other.value());
            return *this;
        }

        T value() const { return value_; }
        T operator*() const { return value(); }
        Value raw_value() const { return value_.raw_value(); }
        friend bool operator==(const Owned2 &left, Value right)
        {
            return left.raw_value() == right;
        }
        friend bool operator!=(const Owned2 &left, Value right)
        {
            return left.raw_value() != right;
        }
        friend bool operator==(Value left, const Owned2 &right)
        {
            return right == left;
        }
        friend bool operator!=(Value left, const Owned2 &right)
        {
            return right != left;
        }

        template <typename U = T,
                  typename ExtractType = decltype(std::declval<U>().extract())>
        ExtractType extract() const
        {
            return value_.extract();
        }

    private:
        void reset(T value)
        {
            incref_value_state(value);
            decref_value_state(value_);
            value_ = value;
        }

        T value_;
    };

    template <typename T> class Member2
    {
    public:
        using semantic_type = typename T::semantic_type;

        Member2() = delete;
        explicit Member2(T value) : value_(value)
        {
            incref_value_state(value_);
        }
        explicit Member2(const Owned2<T> &other) : value_(other.value())
        {
            incref_value_state(value_);
        }

        Member2(const Member2 &other) : value_(other.value_)
        {
            incref_value_state(value_);
        }

        Member2 &operator=(T value)
        {
            reset(value);
            return *this;
        }

        Member2 &operator=(const Member2 &other)
        {
            if(this != &other)
            {
                reset(other.value_);
            }
            return *this;
        }

        Member2 &operator=(const Owned2<T> &other)
        {
            reset(other.value());
            return *this;
        }

        T value() const { return value_; }
        T operator*() const { return value(); }
        Value raw_value() const { return value_.raw_value(); }
        friend bool operator==(const Member2 &left, Value right)
        {
            return left.raw_value() == right;
        }
        friend bool operator!=(const Member2 &left, Value right)
        {
            return left.raw_value() != right;
        }
        friend bool operator==(Value left, const Member2 &right)
        {
            return right == left;
        }
        friend bool operator!=(Value left, const Member2 &right)
        {
            return right != left;
        }

        template <typename U = T,
                  typename ExtractType = decltype(std::declval<U>().extract())>
        ExtractType extract() const
        {
            return value_.extract();
        }

        // For custom heap-object dealloc paths. Leaves the member value
        // unchanged; the containing object must not use the member afterward.
        void release_ref() { decref_value_state(value_); }

    private:
        void reset(T value)
        {
            incref_value_state(value);
            decref_value_state(value_);
            value_ = value;
        }

        T value_;
    };

}  // namespace cl

#endif  // CL_OWNED2_H
