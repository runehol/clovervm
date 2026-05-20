#ifndef CL_OWNED_H
#define CL_OWNED_H

#include "refcount.h"
#include "typed_value.h"
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

        template <typename U = T, typename = std::enable_if_t<
                                      std::is_default_constructible_v<U> &&
                                      !std::is_same_v<U, Value>>>
        Owned() : value_()
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

        template <typename U = T,
                  typename ExtractType = decltype(std::declval<U>().extract())>
        ExtractType extract() const
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

        template <typename U = T, typename = std::enable_if_t<
                                      std::is_default_constructible_v<U> &&
                                      !std::is_same_v<U, Value>>>
        Member() : value_()
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

        template <typename U = T,
                  typename ExtractType = decltype(std::declval<U>().extract())>
        ExtractType extract() const
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

    template <typename T> class OwnedHeapPtr
    {
    public:
        OwnedHeapPtr() : ptr_(nullptr) {}
        OwnedHeapPtr(std::nullptr_t) : ptr_(nullptr) {}
        explicit OwnedHeapPtr(T *ptr) : ptr_(retain_ref(ptr)) {}
        explicit OwnedHeapPtr(HeapPtr<T> ptr) : OwnedHeapPtr(ptr.get()) {}

        OwnedHeapPtr(const OwnedHeapPtr &other) : ptr_(retain_ref(other.ptr_))
        {
        }

        ~OwnedHeapPtr() { release_ptr(ptr_); }

        OwnedHeapPtr &operator=(T *ptr)
        {
            assign(ptr);
            return *this;
        }
        OwnedHeapPtr &operator=(std::nullptr_t)
        {
            assign(nullptr);
            return *this;
        }
        OwnedHeapPtr &operator=(HeapPtr<T> ptr)
        {
            assign(ptr.get());
            return *this;
        }
        OwnedHeapPtr &operator=(const OwnedHeapPtr &other)
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

    template <typename T> class MemberHeapPtr
    {
    public:
        MemberHeapPtr() : ptr_(nullptr) {}
        MemberHeapPtr(std::nullptr_t) : ptr_(nullptr) {}
        explicit MemberHeapPtr(T *ptr) : ptr_(retain_ref(ptr)) {}
        explicit MemberHeapPtr(HeapPtr<T> ptr) : MemberHeapPtr(ptr.get()) {}

        MemberHeapPtr(const MemberHeapPtr &other) : ptr_(retain_ref(other.ptr_))
        {
        }

        MemberHeapPtr &operator=(T *ptr)
        {
            assign(ptr);
            return *this;
        }
        MemberHeapPtr &operator=(std::nullptr_t)
        {
            assign(nullptr);
            return *this;
        }
        MemberHeapPtr &operator=(HeapPtr<T> ptr)
        {
            assign(ptr.get());
            return *this;
        }
        MemberHeapPtr &operator=(const MemberHeapPtr &other)
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

    static_assert(sizeof(OwnedHeapPtr<HeapObject>) == sizeof(HeapObject *));
    static_assert(sizeof(MemberHeapPtr<HeapObject>) == sizeof(HeapObject *));

}  // namespace cl

#endif  // CL_OWNED_H
