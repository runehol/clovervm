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
            reset(static_cast<Handle>(other));
            return *this;
        }

        template <typename H = Handle,
                  typename Extracted = typename HandleTraits<H>::extracted_type,
                  typename = std::enable_if_t<!std::is_void_v<Extracted>>>
        Extracted extract() const
        {
            return HandleTraits<Handle>::extract(handle_);
        }

        Value as_value() const
        {
            return HandleTraits<Handle>::to_value(handle_);
        }
        operator Value() const
        {
            return HandleTraits<Handle>::to_value(handle_);
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        operator Handle() const
        {
            return handle_;
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
            reset(static_cast<Handle>(other));
            return *this;
        }

        template <typename H = Handle,
                  typename Extracted = typename HandleTraits<H>::extracted_type,
                  typename = std::enable_if_t<!std::is_void_v<Extracted>>>
        Extracted extract() const
        {
            return HandleTraits<Handle>::extract(handle_);
        }

        Value as_value() const
        {
            return HandleTraits<Handle>::to_value(handle_);
        }
        operator Value() const
        {
            return HandleTraits<Handle>::to_value(handle_);
        }

        template <typename H = Handle,
                  typename = std::enable_if_t<!std::is_same_v<H, Value>>>
        operator Handle() const
        {
            return handle_;
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
        Handle handle_;
    };

    using OwnedValue = Owned<Value>;
    using MemberValue = Member<Value>;

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
        OwnedHeapPtr(OwnedHeapPtr &&other) noexcept : ptr_(other.release()) {}

        ~OwnedHeapPtr() { release_ref(ptr_); }

        OwnedHeapPtr &operator=(T *ptr)
        {
            reset(ptr);
            return *this;
        }
        OwnedHeapPtr &operator=(std::nullptr_t)
        {
            clear();
            return *this;
        }
        OwnedHeapPtr &operator=(HeapPtr<T> ptr)
        {
            reset(ptr.get());
            return *this;
        }
        OwnedHeapPtr &operator=(const OwnedHeapPtr &other)
        {
            if(this != &other)
            {
                reset(other.ptr_);
            }
            return *this;
        }
        OwnedHeapPtr &operator=(OwnedHeapPtr &&other) noexcept
        {
            if(this != &other)
            {
                release_ref(ptr_);
                ptr_ = other.release();
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

        void reset(T *ptr)
        {
            T *new_ptr = retain_ref(ptr);
            release_ref(ptr_);
            ptr_ = new_ptr;
        }

        void clear()
        {
            release_ref(ptr_);
            ptr_ = nullptr;
        }

        T *release()
        {
            T *released = ptr_;
            ptr_ = nullptr;
            return released;
        }

    private:
        static T *retain_ref(T *ptr)
        {
            incref_heap_ptr(ptr);
            return ptr;
        }

        static void release_ref(T *ptr) { decref_heap_ptr(ptr); }

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
        MemberHeapPtr(MemberHeapPtr &&other) noexcept : ptr_(other.release()) {}

        MemberHeapPtr &operator=(T *ptr)
        {
            reset(ptr);
            return *this;
        }
        MemberHeapPtr &operator=(std::nullptr_t)
        {
            clear();
            return *this;
        }
        MemberHeapPtr &operator=(HeapPtr<T> ptr)
        {
            reset(ptr.get());
            return *this;
        }
        MemberHeapPtr &operator=(const MemberHeapPtr &other)
        {
            if(this != &other)
            {
                reset(other.ptr_);
            }
            return *this;
        }
        MemberHeapPtr &operator=(MemberHeapPtr &&other) noexcept
        {
            if(this != &other)
            {
                release_ref(ptr_);
                ptr_ = other.release();
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

        void reset(T *ptr)
        {
            T *new_ptr = retain_ref(ptr);
            release_ref(ptr_);
            ptr_ = new_ptr;
        }

        void clear()
        {
            release_ref(ptr_);
            ptr_ = nullptr;
        }

        T *release()
        {
            T *released = ptr_;
            ptr_ = nullptr;
            return released;
        }

    private:
        static T *retain_ref(T *ptr)
        {
            incref_heap_ptr(ptr);
            return ptr;
        }

        static void release_ref(T *ptr) { decref_heap_ptr(ptr); }

        T *ptr_;
    };

    static_assert(sizeof(OwnedValue) == sizeof(Value));
    static_assert(sizeof(MemberValue) == sizeof(Value));
    static_assert(sizeof(OwnedHeapPtr<HeapObject>) == sizeof(HeapObject *));
    static_assert(sizeof(MemberHeapPtr<HeapObject>) == sizeof(HeapObject *));
    static_assert(std::is_trivially_destructible_v<MemberValue>);

}  // namespace cl

#endif  // CL_OWNED_H
