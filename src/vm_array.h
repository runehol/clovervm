#ifndef CL_VM_ARRAY_H
#define CL_VM_ARRAY_H

#include "object.h"
#include "owned.h"
#include "refcount.h"
#include "thread_state.h"
#include "typed_value.h"
#include "value.h"
#include "vm_array_backing.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace cl
{
    namespace detail
    {
        inline size_t checked_array_size(size_t size)
        {
            assert(size <= static_cast<size_t>(INT64_MAX >> value_tag_bits));
            return size;
        }

        inline size_t grown_capacity(size_t current_capacity,
                                     size_t minimum_capacity)
        {
            size_t grown = current_capacity == 0 ? 1 : current_capacity * 2;
            if(grown < minimum_capacity)
            {
                grown = minimum_capacity;
            }
            assert(grown <= static_cast<size_t>(INT64_MAX >> value_tag_bits));
            return grown;
        }
    }  // namespace detail

    template <typename T> class RawArray
    {
    private:
        static_assert(std::is_standard_layout_v<T>);
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(alignof(T) <= alignof(std::max_align_t));

        using Backing = RawArrayBacking;
        static_assert(std::is_trivially_destructible_v<Backing>);

        static size_t storage_size_for_capacity(size_t capacity)
        {
            assert(capacity >= 1);
            return sizeof(T) * capacity;
        }

    public:
        static constexpr uint64_t embedded_value_count = 3;

        using value_type = T;
        using size_type = size_t;
        using iterator = T *;
        using const_iterator = const T *;

        RawArray()
            : size_value(TValue2<SMI>::from_smi(0)),
              capacity_value(TValue2<SMI>::from_smi(0)), backing(nullptr)
        {
        }

        explicit RawArray(size_t count) : RawArray() { resize(count); }

        RawArray(size_t count, const T &value) : RawArray()
        {
            resize(count, value);
        }

        RawArray(const RawArray &) = delete;
        RawArray &operator=(const RawArray &) = delete;
        RawArray(RawArray &&) = delete;
        RawArray &operator=(RawArray &&) = delete;

        size_t size() const { return static_cast<size_t>(len().extract()); }

        TValue2<SMI> len() const { return size_value.value(); }

        size_t capacity() const
        {
            return static_cast<size_t>(capacity_value.extract());
        }

        bool empty() const { return size() == 0; }

        T *data()
        {
            return backing == nullptr
                       ? nullptr
                       : reinterpret_cast<T *>(backing_ptr()->bytes);
        }

        const T *data() const
        {
            return backing == nullptr
                       ? nullptr
                       : reinterpret_cast<const T *>(backing_ptr()->bytes);
        }

        iterator begin() { return data(); }
        const_iterator begin() const { return data(); }
        iterator end() { return data() + size(); }
        const_iterator end() const { return data() + size(); }

        T &get(size_t idx) { return (*this)[idx]; }
        T &operator[](size_t idx)
        {
            assert(idx < size());
            return data()[idx];
        }

        const T &get(size_t idx) const { return (*this)[idx]; }
        const T &operator[](size_t idx) const
        {
            assert(idx < size());
            return data()[idx];
        }

        T &front()
        {
            assert(!empty());
            return (*this)[0];
        }

        const T &front() const
        {
            assert(!empty());
            return (*this)[0];
        }

        T &back()
        {
            assert(!empty());
            return (*this)[size() - 1];
        }

        const T &back() const
        {
            assert(!empty());
            return (*this)[size() - 1];
        }

        void clear() { set_size(0); }

        void reserve(size_t new_capacity)
        {
            size_t requested_capacity =
                detail::checked_array_size(new_capacity);
            if(requested_capacity <= capacity())
            {
                return;
            }

            Backing *new_backing = make_internal_raw<Backing>(
                storage_size_for_capacity(requested_capacity));
            T *new_data = reinterpret_cast<T *>(new_backing->bytes);
            size_t current_size = size();
            for(size_t idx = 0; idx < current_size; ++idx)
            {
                new(new_data + idx) T((*this)[idx]);
            }

            backing = new_backing;
            set_capacity(requested_capacity);
        }

        void resize(size_t new_size) { resize(new_size, T()); }

        void resize(size_t new_size, const T &value)
        {
            size_t requested_size = detail::checked_array_size(new_size);
            size_t current_size = size();
            if(requested_size < current_size)
            {
                set_size(requested_size);
                return;
            }

            reserve(requested_size);
            while(current_size < requested_size)
            {
                new(data() + current_size) T(value);
                ++current_size;
            }
            set_size(current_size);
        }

        void assign(size_t count, const T &value)
        {
            size_t requested_size = detail::checked_array_size(count);
            reserve(requested_size);
            for(size_t idx = 0; idx < requested_size; ++idx)
            {
                new(data() + idx) T(value);
            }
            set_size(requested_size);
        }

        void set(size_t idx, const T &value)
        {
            assert(idx < size());
            data()[idx] = value;
        }

        template <typename... Args> T &emplace_back(Args &&...args)
        {
            size_t current_size = size();
            if(current_size == capacity())
            {
                reserve(detail::grown_capacity(capacity(), current_size + 1));
            }

            new(data() + current_size) T(std::forward<Args>(args)...);
            set_size(current_size + 1);
            return back();
        }

        void push_back(const T &value) { emplace_back(value); }

    private:
        void set_size(size_t new_size)
        {
            size_value = TValue2<SMI>::from_smi(static_cast<int64_t>(new_size));
        }

        void set_capacity(size_t new_capacity)
        {
            capacity_value =
                TValue2<SMI>::from_smi(static_cast<int64_t>(new_capacity));
        }

        Backing *backing_ptr() { return backing.extract(); }
        const Backing *backing_ptr() const { return backing.extract(); }

        Member<TValue2<SMI>> size_value;
        Member<TValue2<SMI>> capacity_value;
        MemberHeapPtr<Backing> backing;
    };

    template <typename T> class ValueArray
    {
    private:
        static_assert(std::is_standard_layout_v<T>);
        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(!std::is_base_of_v<HeapObject, T>);
        static_assert(alignof(T) <= alignof(Value));
        static_assert(sizeof(T) % sizeof(Value) == 0);

        static constexpr size_t values_per_element = sizeof(T) / sizeof(Value);
        using Backing = ValueArrayBacking;
        static_assert(std::is_trivially_destructible_v<Backing>);

        static size_t value_cell_count_for_capacity(size_t capacity)
        {
            return capacity * values_per_element;
        }

    public:
        static constexpr uint64_t embedded_value_count = 3;

        using value_type = T;
        using size_type = size_t;
        using const_iterator = const T *;

        ValueArray()
            : size_value(TValue2<SMI>::from_smi(0)),
              capacity_value(TValue2<SMI>::from_smi(0)), backing(nullptr)
        {
        }

        explicit ValueArray(size_t count) : ValueArray() { resize(count); }

        ValueArray(size_t count, const T &value) : ValueArray()
        {
            resize(count, value);
        }

        ValueArray(const ValueArray &) = delete;
        ValueArray &operator=(const ValueArray &) = delete;
        ValueArray(ValueArray &&) = delete;
        ValueArray &operator=(ValueArray &&) = delete;

        size_t size() const { return static_cast<size_t>(len().extract()); }

        TValue2<SMI> len() const { return size_value.value(); }

        size_t capacity() const
        {
            return static_cast<size_t>(capacity_value.extract());
        }

        bool empty() const { return size() == 0; }

        const T *data() const
        {
            return backing == nullptr
                       ? nullptr
                       : reinterpret_cast<const T *>(backing_ptr()->elements);
        }

        const_iterator begin() const { return data(); }
        const_iterator end() const { return data() + size(); }

        const T get(size_t idx) const { return (*this)[idx]; }

        const T operator[](size_t idx) const
        {
            assert(idx < size());
            return data()[idx];
        }

        const T front() const
        {
            assert(!empty());
            return (*this)[0];
        }

        const T back() const
        {
            assert(!empty());
            return (*this)[size() - 1];
        }

        void clear()
        {
            clear_elements(0, size());
            set_size(0);
        }

        void reserve(size_t new_capacity)
        {
            size_t requested_capacity =
                detail::checked_array_size(new_capacity);
            if(requested_capacity <= capacity())
            {
                return;
            }

            Backing *new_backing = make_internal_raw<Backing>(
                value_cell_count_for_capacity(requested_capacity));
            std::memset(new_backing->elements, 0,
                        sizeof(Value) *
                            value_cell_count_for_capacity(requested_capacity));

            T *new_data = reinterpret_cast<T *>(new_backing->elements);
            size_t current_size = size();
            for(size_t idx = 0; idx < current_size; ++idx)
            {
                new(new_data + idx) T((*this)[idx]);
            }

            if(backing != nullptr)
            {
                backing.extract()->set_value_cell_count(0);
            }

            backing = new_backing;
            set_capacity(requested_capacity);
        }

        void resize(size_t new_size)
        {
            static_assert(std::is_default_constructible_v<T>);
            resize(new_size, T());
        }

        void resize(size_t new_size, const T &value)
        {
            size_t requested_size = detail::checked_array_size(new_size);
            size_t current_size = size();
            if(requested_size < current_size)
            {
                clear_elements(requested_size, current_size);
                set_size(requested_size);
                return;
            }

            reserve(requested_size);
            while(current_size < requested_size)
            {
                T *slot = mutable_data() + current_size;
                new(slot) T(value);
                incref_element(slot);
                ++current_size;
            }
            set_size(current_size);
        }

        void set(size_t idx, T value)
        {
            assert(idx < size());
            T *slot = mutable_data() + idx;
            clear_element(slot);
            new(slot) T(value);
            incref_element(slot);
        }

        HeapObject *swap_slot(size_t idx, Value value)
        {
            static_assert(std::is_same_v<T, Value>);
            assert(idx < size());
            Value *slot = mutable_data() + idx;
            Value old = *slot;
            if(old == value)
            {
                return nullptr;
            }

            if(value.is_refcounted_ptr())
            {
                ++value.as.ptr->refcount;
            }
            *slot = value;
            if(old.is_refcounted_ptr())
            {
                if(--old.as.ptr->refcount == 0)
                {
                    return old.as.ptr;
                }
            }
            return nullptr;
        }

        template <typename... Args> const T emplace_back(Args &&...args)
        {
            size_t current_size = size();
            if(current_size == capacity())
            {
                reserve(detail::grown_capacity(capacity(), current_size + 1));
            }

            T *slot = mutable_data() + current_size;
            new(slot) T(std::forward<Args>(args)...);
            incref_element(slot);
            set_size(current_size + 1);
            return *slot;
        }

        void push_back(const T &value) { emplace_back(value); }

    private:
        static Value *element_cells(T *element)
        {
            return reinterpret_cast<Value *>(element);
        }

        static const Value *element_cells(const T *element)
        {
            return reinterpret_cast<const Value *>(element);
        }

        T *mutable_data()
        {
            return backing == nullptr
                       ? nullptr
                       : reinterpret_cast<T *>(backing_ptr()->elements);
        }

        static void incref_element(T *element)
        {
            Value *cells = element_cells(element);
            for(size_t idx = 0; idx < values_per_element; ++idx)
            {
                cells[idx] = incref(cells[idx]);
            }
        }

        static void clear_element(T *element)
        {
            Value *cells = element_cells(element);
            for(size_t idx = 0; idx < values_per_element; ++idx)
            {
                decref(cells[idx]);
                cells[idx] = Value::from_smi(0);
            }
        }

        void clear_elements(size_t start_idx, size_t end_idx)
        {
            if(backing == nullptr)
            {
                return;
            }

            for(size_t idx = start_idx; idx < end_idx; ++idx)
            {
                clear_element(mutable_data() + idx);
            }
        }

        void set_size(size_t new_size)
        {
            size_value = TValue2<SMI>::from_smi(static_cast<int64_t>(new_size));
        }

        void set_capacity(size_t new_capacity)
        {
            capacity_value =
                TValue2<SMI>::from_smi(static_cast<int64_t>(new_capacity));
        }

        Backing *backing_ptr() { return backing.extract(); }
        const Backing *backing_ptr() const { return backing.extract(); }

        Member<TValue2<SMI>> size_value;
        Member<TValue2<SMI>> capacity_value;
        MemberHeapPtr<Backing> backing;
    };

    template <typename T> class HeapPtrArray
    {
    private:
        static_assert(std::is_base_of_v<HeapObject, T>);

        using Backing = HeapPtrArrayBacking;
        static_assert(std::is_trivially_destructible_v<Backing>);

    public:
        static constexpr uint64_t embedded_value_count = 3;

        using value_type = T *;
        using size_type = size_t;
        using const_iterator = T *const *;

        HeapPtrArray()
            : size_value(TValue2<SMI>::from_smi(0)),
              capacity_value(TValue2<SMI>::from_smi(0)), backing(nullptr)
        {
        }

        explicit HeapPtrArray(size_t count) : HeapPtrArray() { resize(count); }

        HeapPtrArray(const HeapPtrArray &) = delete;
        HeapPtrArray &operator=(const HeapPtrArray &) = delete;
        HeapPtrArray(HeapPtrArray &&) = delete;
        HeapPtrArray &operator=(HeapPtrArray &&) = delete;

        size_t size() const { return static_cast<size_t>(len().extract()); }

        TValue2<SMI> len() const { return size_value.value(); }

        size_t capacity() const
        {
            return static_cast<size_t>(capacity_value.extract());
        }

        bool empty() const { return size() == 0; }

        T *const *data() const
        {
            return backing == nullptr
                       ? nullptr
                       : reinterpret_cast<T *const *>(backing_ptr()->elements);
        }

        const_iterator begin() const { return data(); }
        const_iterator end() const { return data() + size(); }

        T *get(size_t idx) const { return (*this)[idx]; }

        T *operator[](size_t idx) const
        {
            assert(idx < size());
            return data()[idx];
        }

        T *front() const
        {
            assert(!empty());
            return (*this)[0];
        }

        T *back() const
        {
            assert(!empty());
            return (*this)[size() - 1];
        }

        void clear()
        {
            clear_elements(0, size());
            set_size(0);
        }

        void reserve(size_t new_capacity)
        {
            size_t requested_capacity =
                detail::checked_array_size(new_capacity);
            if(requested_capacity <= capacity())
            {
                return;
            }

            Backing *new_backing =
                make_internal_raw<Backing>(requested_capacity);
            std::memset(new_backing->elements, 0,
                        sizeof(HeapObject *) * requested_capacity);

            T **new_data = reinterpret_cast<T **>(new_backing->elements);
            size_t current_size = size();
            for(size_t idx = 0; idx < current_size; ++idx)
            {
                new_data[idx] = data()[idx];
            }

            if(backing != nullptr)
            {
                backing.extract()->set_value_cell_count(0);
            }
            backing = new_backing;
            set_capacity(requested_capacity);
        }

        void resize(size_t new_size)
        {
            size_t requested_size = detail::checked_array_size(new_size);
            size_t current_size = size();
            if(requested_size < current_size)
            {
                clear_elements(requested_size, current_size);
                set_size(requested_size);
                return;
            }

            reserve(requested_size);
            while(current_size < requested_size)
            {
                mutable_data()[current_size] = nullptr;
                ++current_size;
            }
            set_size(current_size);
        }

        void set(size_t idx, T *value)
        {
            assert(idx < size());
            T **slot = mutable_data() + idx;
            decref_heap_ptr(*slot);
            *slot = static_cast<T *>(incref_heap_ptr(value));
        }

        void push_back(T *value)
        {
            size_t current_size = size();
            if(current_size == capacity())
            {
                reserve(detail::grown_capacity(capacity(), current_size + 1));
            }

            mutable_data()[current_size] =
                static_cast<T *>(incref_heap_ptr(value));
            set_size(current_size + 1);
        }

    private:
        T **mutable_data()
        {
            return backing == nullptr
                       ? nullptr
                       : reinterpret_cast<T **>(backing_ptr()->elements);
        }

        void clear_elements(size_t start_idx, size_t end_idx)
        {
            if(backing == nullptr)
            {
                return;
            }

            T **items = mutable_data();
            for(size_t idx = start_idx; idx < end_idx; ++idx)
            {
                decref_heap_ptr(items[idx]);
                items[idx] = nullptr;
            }
        }

        void set_size(size_t new_size)
        {
            size_value = TValue2<SMI>::from_smi(static_cast<int64_t>(new_size));
        }

        void set_capacity(size_t new_capacity)
        {
            capacity_value =
                TValue2<SMI>::from_smi(static_cast<int64_t>(new_capacity));
        }

        Backing *backing_ptr() { return backing.extract(); }
        const Backing *backing_ptr() const { return backing.extract(); }

        Member<TValue2<SMI>> size_value;
        Member<TValue2<SMI>> capacity_value;
        MemberHeapPtr<Backing> backing;
    };

}  // namespace cl

#endif  // CL_VM_ARRAY_H
