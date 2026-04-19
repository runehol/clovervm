#ifndef CL_VM_ARRAY_H
#define CL_VM_ARRAY_H

#include "klass.h"
#include "object.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "thread_state.h"
#include "typed_value.h"
#include "value.h"
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

        class Backing : public Object
        {
        public:
            using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
            static constexpr Klass klass = Klass(L"RawArrayBacking", nullptr);

            explicit Backing(size_t capacity) : Object(&klass)
            {
                (void)capacity;
            }

            static size_t size_for(size_t capacity)
            {
                assert(capacity >= 1);
                return sizeof(Backing) + sizeof(Storage) * capacity -
                       sizeof(Storage);
            }

            static DynamicLayoutSpec layout_spec_for(size_t capacity)
            {
                return DynamicLayoutSpec{
                    round_up_to_16byte_units(size_for(capacity)), 0};
            }

            Storage elements[1];

            CL_DECLARE_DYNAMIC_LAYOUT_NO_VALUES(Backing);
        };

        static_assert(std::is_trivially_destructible_v<Backing>);

    public:
        static constexpr uint64_t embedded_value_count = 3;

        using value_type = T;
        using size_type = size_t;
        using iterator = T *;
        using const_iterator = const T *;

        RawArray()
            : size_value(Value::from_smi(0)),
              capacity_value(Value::from_smi(0)), backing(Value::None())
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

        TValue<SMI> len() const
        {
            return TValue<SMI>::unsafe_unchecked(size_value.as_value());
        }

        size_t capacity() const
        {
            return static_cast<size_t>(capacity_value.extract());
        }

        bool empty() const { return size() == 0; }

        T *data()
        {
            return backing == Value::None()
                       ? nullptr
                       : reinterpret_cast<T *>(backing_ptr()->elements);
        }

        const T *data() const
        {
            return backing == Value::None()
                       ? nullptr
                       : reinterpret_cast<const T *>(backing_ptr()->elements);
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

            Backing *new_backing =
                ThreadState::get_active()->make_refcounted_raw<Backing>(
                    requested_capacity);
            T *new_data = reinterpret_cast<T *>(new_backing->elements);
            size_t current_size = size();
            for(size_t idx = 0; idx < current_size; ++idx)
            {
                new(new_data + idx) T((*this)[idx]);
            }

            backing = Value::from_oop(new_backing);
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
            size_value = Value::from_smi(static_cast<int64_t>(new_size));
        }

        void set_capacity(size_t new_capacity)
        {
            capacity_value =
                Value::from_smi(static_cast<int64_t>(new_capacity));
        }

        Backing *backing_ptr()
        {
            return backing.as_value().template get_ptr<Backing>();
        }
        const Backing *backing_ptr() const
        {
            return backing.as_value().template get_ptr<Backing>();
        }

        MemberTValue<SMI> size_value;
        MemberTValue<SMI> capacity_value;
        MemberValue backing;
    };

    template <typename T> class ValueArray
    {
    private:
        static_assert(std::is_standard_layout_v<T>);
        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(!std::is_base_of_v<Object, T>);
        static_assert(alignof(T) <= alignof(Value));
        static_assert(sizeof(T) % sizeof(Value) == 0);

        static constexpr size_t values_per_element = sizeof(T) / sizeof(Value);

        class Backing : public Object
        {
        public:
            static constexpr Klass klass = Klass(L"ValueArrayBacking", nullptr);

            explicit Backing(size_t capacity) : Object(&klass)
            {
                (void)capacity;
            }

            static size_t size_for(size_t capacity)
            {
                assert(capacity >= 1);
                return sizeof(Backing) +
                       sizeof(Value) * values_per_element * capacity -
                       sizeof(Value);
            }

            static DynamicLayoutSpec layout_spec_for(size_t capacity)
            {
                return DynamicLayoutSpec{
                    round_up_to_16byte_units(size_for(capacity)),
                    uint64_t(capacity) * values_per_element};
            }

            Value elements[1];

            CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(Backing, elements);
        };

        static_assert(std::is_trivially_destructible_v<Backing>);

    public:
        static constexpr uint64_t embedded_value_count = 3;

        using value_type = T;
        using size_type = size_t;
        using const_iterator = const T *;

        ValueArray()
            : size_value(Value::from_smi(0)),
              capacity_value(Value::from_smi(0)), backing(Value::None())
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

        TValue<SMI> len() const
        {
            return TValue<SMI>::unsafe_unchecked(size_value.as_value());
        }

        size_t capacity() const
        {
            return static_cast<size_t>(capacity_value.extract());
        }

        bool empty() const { return size() == 0; }

        const T *data() const
        {
            return backing == Value::None()
                       ? nullptr
                       : reinterpret_cast<const T *>(backing_ptr()->elements);
        }

        const_iterator begin() const { return data(); }
        const_iterator end() const { return data() + size(); }

        T get(size_t idx) const { return (*this)[idx]; }

        T operator[](size_t idx) const
        {
            assert(idx < size());
            return data()[idx];
        }

        T front() const
        {
            assert(!empty());
            return (*this)[0];
        }

        T back() const
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
                ThreadState::get_active()->make_refcounted_raw<Backing>(
                    requested_capacity);
            std::memset(new_backing->elements, 0,
                        sizeof(Value) * requested_capacity *
                            values_per_element);

            T *new_data = reinterpret_cast<T *>(new_backing->elements);
            size_t current_size = size();
            for(size_t idx = 0; idx < current_size; ++idx)
            {
                new(new_data + idx) T((*this)[idx]);
                incref_element(new_data + idx);
            }

            if(backing != Value::None())
            {
                object_clear_value_ownership(
                    backing.as_value().template get_ptr<Object>());
            }
            backing = Value::from_oop(new_backing);
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

        template <typename... Args> T emplace_back(Args &&...args)
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
            return backing == Value::None()
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
            if(backing == Value::None())
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
            size_value = Value::from_smi(static_cast<int64_t>(new_size));
        }

        void set_capacity(size_t new_capacity)
        {
            capacity_value =
                Value::from_smi(static_cast<int64_t>(new_capacity));
        }

        Backing *backing_ptr()
        {
            return backing.as_value().template get_ptr<Backing>();
        }
        const Backing *backing_ptr() const
        {
            return backing.as_value().template get_ptr<Backing>();
        }

        MemberTValue<SMI> size_value;
        MemberTValue<SMI> capacity_value;
        MemberValue backing;
    };

}  // namespace cl

#endif  // CL_VM_ARRAY_H
