#ifndef CL_HEAP_PTR_H
#define CL_HEAP_PTR_H

#include "object_model/heap_object.h"
#include "object_model/value_tags.h"
#include <cassert>
#include <cstddef>
#include <type_traits>

namespace cl
{
    static inline bool heap_ptr_is_refcounted(HeapObject *obj)
    {
        return (reinterpret_cast<uintptr_t>(obj) & value_ptr_mask) ==
               value_refcounted_ptr_tag;
    }

    static inline bool heap_ptr_is_interned(HeapObject *obj)
    {
        return (reinterpret_cast<uintptr_t>(obj) & value_ptr_mask) ==
               value_interned_ptr_tag;
    }

    static inline HeapObject *incref_heap_ptr(HeapObject *obj)
    {
        assert(obj == nullptr || heap_ptr_is_refcounted(obj) ||
               heap_ptr_is_interned(obj));
        if(heap_ptr_is_refcounted(obj))
        {
            ++obj->refcount;
        }
        return obj;
    }

    template <typename T> T *incref(T *t)
    {
        HeapObject *obj = const_cast<typename std::remove_const<T>::type *>(t);
        incref_heap_ptr(obj);
        return t;
    }

    void add_to_active_zero_count_table_if_needed(HeapObject *obj);

    static inline void decref_heap_ptr(HeapObject *obj)
    {
        assert(obj == nullptr || heap_ptr_is_refcounted(obj) ||
               heap_ptr_is_interned(obj));
        if(heap_ptr_is_refcounted(obj) && --obj->refcount == 0)
        {
            add_to_active_zero_count_table_if_needed(obj);
        }
    }

    template <typename T> T *decref(T *t)
    {
        HeapObject *obj = const_cast<typename std::remove_const<T>::type *>(t);
        decref_heap_ptr(obj);
        return t;
    }

    template <typename T> class HeapPtr
    {
    public:
        HeapPtr() : ptr_(nullptr) {}
        HeapPtr(std::nullptr_t) : ptr_(nullptr) {}
        HeapPtr(T *ptr) : ptr_(ptr) {}

        T *get() const { return ptr_; }
        T *extract() const { return ptr_; }

        explicit operator bool() const { return ptr_ != nullptr; }
        bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
        bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    private:
        T *ptr_;
    };

    static_assert(sizeof(HeapPtr<HeapObject>) == sizeof(HeapObject *));

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

#endif  // CL_HEAP_PTR_H
