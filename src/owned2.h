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

        template <typename U = T, typename = std::enable_if_t<
                                      std::is_default_constructible_v<U> &&
                                      !std::is_same_v<U, Value>>>
        Owned2() : value_()
        {
            incref_value_state(value_);
        }
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
            assign(value);
            return *this;
        }

        Owned2 &operator=(const Owned2 &other)
        {
            if(this != &other)
            {
                assign(other.value_);
            }
            return *this;
        }

        Owned2 &operator=(const Member2<T> &other)
        {
            assign(other.value());
            return *this;
        }

        T value() const { return value_; }
        T operator*() const { return value(); }
        operator T() const { return value(); }
        Value raw_value() const { return value_.raw_value(); }

        template <typename U = T,
                  typename ExtractType = decltype(std::declval<U>().extract())>
        ExtractType extract() const
        {
            return value_.extract();
        }

    private:
        void assign(T value)
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

        template <typename U = T, typename = std::enable_if_t<
                                      std::is_default_constructible_v<U> &&
                                      !std::is_same_v<U, Value>>>
        Member2() : value_()
        {
            incref_value_state(value_);
        }
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
            assign(value);
            return *this;
        }

        Member2 &operator=(const Member2 &other)
        {
            if(this != &other)
            {
                assign(other.value_);
            }
            return *this;
        }

        Member2 &operator=(const Owned2<T> &other)
        {
            assign(other.value());
            return *this;
        }

        T value() const { return value_; }
        T operator*() const { return value(); }
        operator T() const { return value(); }
        Value raw_value() const { return value_.raw_value(); }

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
        void assign(T value)
        {
            incref_value_state(value);
            decref_value_state(value_);
            value_ = value;
        }

        T value_;
    };

    template <typename T> class OwnedHeapPtr2
    {
    public:
        OwnedHeapPtr2() : ptr_(nullptr) {}
        OwnedHeapPtr2(std::nullptr_t) : ptr_(nullptr) {}
        explicit OwnedHeapPtr2(T *ptr) : ptr_(retain_ref(ptr)) {}
        explicit OwnedHeapPtr2(HeapPtr<T> ptr) : OwnedHeapPtr2(ptr.get()) {}

        OwnedHeapPtr2(const OwnedHeapPtr2 &other) : ptr_(retain_ref(other.ptr_))
        {
        }

        ~OwnedHeapPtr2() { release_ptr(ptr_); }

        OwnedHeapPtr2 &operator=(T *ptr)
        {
            assign(ptr);
            return *this;
        }
        OwnedHeapPtr2 &operator=(std::nullptr_t)
        {
            assign(nullptr);
            return *this;
        }
        OwnedHeapPtr2 &operator=(HeapPtr<T> ptr)
        {
            assign(ptr.get());
            return *this;
        }
        OwnedHeapPtr2 &operator=(const OwnedHeapPtr2 &other)
        {
            if(this != &other)
            {
                assign(other.ptr_);
            }
            return *this;
        }

        T *get() const { return ptr_; }
        T *extract() const { return ptr_; }
        T *operator->() const { return ptr_; }
        explicit operator bool() const { return ptr_ != nullptr; }
        operator HeapPtr<T>() const { return HeapPtr<T>(ptr_); }
        bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
        bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    private:
        void assign(T *ptr)
        {
            T *new_ptr = retain_ref(ptr);
            release_ptr(ptr_);
            ptr_ = new_ptr;
        }

        static T *retain_ref(T *ptr)
        {
            incref_heap_ptr(ptr);
            return ptr;
        }

        static void release_ptr(T *ptr) { decref_heap_ptr(ptr); }

        T *ptr_;
    };

    template <typename T> class MemberHeapPtr2
    {
    public:
        MemberHeapPtr2() : ptr_(nullptr) {}
        MemberHeapPtr2(std::nullptr_t) : ptr_(nullptr) {}
        explicit MemberHeapPtr2(T *ptr) : ptr_(retain_ref(ptr)) {}
        explicit MemberHeapPtr2(HeapPtr<T> ptr) : MemberHeapPtr2(ptr.get()) {}

        MemberHeapPtr2(const MemberHeapPtr2 &other)
            : ptr_(retain_ref(other.ptr_))
        {
        }

        MemberHeapPtr2 &operator=(T *ptr)
        {
            assign(ptr);
            return *this;
        }
        MemberHeapPtr2 &operator=(std::nullptr_t)
        {
            assign(nullptr);
            return *this;
        }
        MemberHeapPtr2 &operator=(HeapPtr<T> ptr)
        {
            assign(ptr.get());
            return *this;
        }
        MemberHeapPtr2 &operator=(const MemberHeapPtr2 &other)
        {
            if(this != &other)
            {
                assign(other.ptr_);
            }
            return *this;
        }

        T *get() const { return ptr_; }
        T *extract() const { return ptr_; }
        T *operator->() const { return ptr_; }
        explicit operator bool() const { return ptr_ != nullptr; }
        operator HeapPtr<T>() const { return HeapPtr<T>(ptr_); }
        bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
        bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

        // For custom heap-object dealloc paths. Leaves the member pointer
        // unchanged; the containing object must not use the member afterward.
        void release_ref() { release_ptr(ptr_); }

    private:
        void assign(T *ptr)
        {
            T *new_ptr = retain_ref(ptr);
            release_ptr(ptr_);
            ptr_ = new_ptr;
        }

        static T *retain_ref(T *ptr)
        {
            incref_heap_ptr(ptr);
            return ptr;
        }

        static void release_ptr(T *ptr) { decref_heap_ptr(ptr); }

        T *ptr_;
    };

    static_assert(sizeof(OwnedHeapPtr2<HeapObject>) == sizeof(HeapObject *));
    static_assert(sizeof(MemberHeapPtr2<HeapObject>) == sizeof(HeapObject *));

}  // namespace cl

#endif  // CL_OWNED2_H
