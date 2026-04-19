#ifndef CL_VM_ARRAY_H
#define CL_VM_ARRAY_H

#include "klass.h"
#include "object.h"
#include "refcount.h"
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
        inline uint32_t checked_array_size(size_t size)
        {
            assert(size <= uint32_t(-1));
            return static_cast<uint32_t>(size);
        }

        inline uint32_t grown_capacity(uint32_t current_capacity,
                                       uint32_t minimum_capacity)
        {
            uint64_t grown =
                current_capacity == 0 ? 1 : uint64_t(current_capacity) * 2;
            if(grown < minimum_capacity)
            {
                grown = minimum_capacity;
            }
            assert(grown <= uint32_t(-1));
            return static_cast<uint32_t>(grown);
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

            explicit Backing(uint32_t capacity) : Object(&klass)
            {
                (void)capacity;
            }

            static size_t size_for(uint32_t capacity)
            {
                assert(capacity >= 1);
                return sizeof(Backing) + sizeof(Storage) * capacity -
                       sizeof(Storage);
            }

            static DynamicLayoutSpec layout_spec_for(uint32_t capacity)
            {
                return DynamicLayoutSpec{
                    round_up_to_16byte_units(size_for(capacity)), 0};
            }

            Storage elements[1];

            CL_DECLARE_DYNAMIC_LAYOUT_NO_VALUES(Backing);
        };

        static_assert(std::is_trivially_destructible_v<Backing>);

    public:
        using value_type = T;
        using size_type = size_t;
        using iterator = T *;
        using const_iterator = const T *;

        RawArray() = default;

        explicit RawArray(size_t count) { resize(count); }

        RawArray(size_t count, const T &value) { resize(count, value); }

        RawArray(RawArray &&other) noexcept
            : backing_(other.backing_), size_(other.size_),
              capacity_(other.capacity_)
        {
            other.backing_ = Value::None();
            other.size_ = 0;
            other.capacity_ = 0;
        }

        RawArray &operator=(RawArray &&other) noexcept
        {
            if(this != &other)
            {
                decref(backing_);
                backing_ = other.backing_;
                size_ = other.size_;
                capacity_ = other.capacity_;
                other.backing_ = Value::None();
                other.size_ = 0;
                other.capacity_ = 0;
            }
            return *this;
        }

        RawArray(const RawArray &) = delete;
        RawArray &operator=(const RawArray &) = delete;

        ~RawArray() { decref(backing_); }

        size_t size() const { return size_; }
        size_t capacity() const { return capacity_; }
        bool empty() const { return size_ == 0; }

        T *data()
        {
            return backing_ == Value::None()
                       ? nullptr
                       : reinterpret_cast<T *>(backing_ptr()->elements);
        }
        const T *data() const
        {
            return backing_ == Value::None()
                       ? nullptr
                       : reinterpret_cast<const T *>(backing_ptr()->elements);
        }

        iterator begin() { return data(); }
        const_iterator begin() const { return data(); }
        iterator end() { return data() + size_; }
        const_iterator end() const { return data() + size_; }

        T &operator[](size_t idx)
        {
            assert(idx < size_);
            return data()[idx];
        }
        const T &operator[](size_t idx) const
        {
            assert(idx < size_);
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
            return (*this)[size_ - 1];
        }
        const T &back() const
        {
            assert(!empty());
            return (*this)[size_ - 1];
        }

        void clear() { size_ = 0; }

        void reserve(size_t new_capacity)
        {
            uint32_t requested_capacity =
                detail::checked_array_size(new_capacity);
            if(requested_capacity <= capacity_)
            {
                return;
            }

            Backing *new_backing =
                ThreadState::get_active()->make_refcounted_raw<Backing>(
                    requested_capacity);
            T *new_data = reinterpret_cast<T *>(new_backing->elements);
            for(uint32_t idx = 0; idx < size_; ++idx)
            {
                new(new_data + idx) T((*this)[idx]);
            }

            Value old_backing = backing_;
            backing_ = incref(Value::from_oop(new_backing));
            capacity_ = requested_capacity;
            decref(old_backing);
        }

        void resize(size_t new_size) { resize(new_size, T()); }

        void resize(size_t new_size, const T &value)
        {
            uint32_t requested_size = detail::checked_array_size(new_size);
            if(requested_size < size_)
            {
                size_ = requested_size;
                return;
            }

            reserve(requested_size);
            while(size_ < requested_size)
            {
                new(data() + size_) T(value);
                ++size_;
            }
        }

        template <typename... Args> T &emplace_back(Args &&...args)
        {
            if(size_ == capacity_)
            {
                reserve(detail::grown_capacity(capacity_, size_ + 1));
            }

            new(data() + size_) T(std::forward<Args>(args)...);
            ++size_;
            return back();
        }

        void push_back(const T &value) { emplace_back(value); }

    private:
        Backing *backing_ptr() { return backing_.get_ptr<Backing>(); }
        const Backing *backing_ptr() const
        {
            return backing_.get_ptr<Backing>();
        }

        Value backing_ = Value::None();
        uint32_t size_ = 0;
        uint32_t capacity_ = 0;
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

            explicit Backing(uint32_t capacity) : Object(&klass)
            {
                (void)capacity;
            }

            static size_t size_for(uint32_t capacity)
            {
                assert(capacity >= 1);
                return sizeof(Backing) +
                       sizeof(Value) * values_per_element * capacity -
                       sizeof(Value);
            }

            static DynamicLayoutSpec layout_spec_for(uint32_t capacity)
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
        using value_type = T;
        using size_type = size_t;
        using iterator = T *;
        using const_iterator = const T *;

        ValueArray() = default;

        explicit ValueArray(size_t count) { resize(count); }

        ValueArray(size_t count, const T &value) { resize(count, value); }

        ValueArray(ValueArray &&other) noexcept
            : backing_(other.backing_), size_(other.size_),
              capacity_(other.capacity_)
        {
            other.backing_ = Value::None();
            other.size_ = 0;
            other.capacity_ = 0;
        }

        ValueArray &operator=(ValueArray &&other) noexcept
        {
            if(this != &other)
            {
                clear();
                decref(backing_);
                backing_ = other.backing_;
                size_ = other.size_;
                capacity_ = other.capacity_;
                other.backing_ = Value::None();
                other.size_ = 0;
                other.capacity_ = 0;
            }
            return *this;
        }

        ValueArray(const ValueArray &) = delete;
        ValueArray &operator=(const ValueArray &) = delete;

        ~ValueArray()
        {
            clear();
            decref(backing_);
        }

        size_t size() const { return size_; }
        size_t capacity() const { return capacity_; }
        bool empty() const { return size_ == 0; }

        T *data()
        {
            return backing_ == Value::None()
                       ? nullptr
                       : reinterpret_cast<T *>(backing_ptr()->elements);
        }
        const T *data() const
        {
            return backing_ == Value::None()
                       ? nullptr
                       : reinterpret_cast<const T *>(backing_ptr()->elements);
        }

        iterator begin() { return data(); }
        const_iterator begin() const { return data(); }
        iterator end() { return data() + size_; }
        const_iterator end() const { return data() + size_; }

        T &operator[](size_t idx)
        {
            assert(idx < size_);
            return data()[idx];
        }
        const T &operator[](size_t idx) const
        {
            assert(idx < size_);
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
            return (*this)[size_ - 1];
        }
        const T &back() const
        {
            assert(!empty());
            return (*this)[size_ - 1];
        }

        void clear()
        {
            clear_elements(0, size_);
            size_ = 0;
        }

        void reserve(size_t new_capacity)
        {
            uint32_t requested_capacity =
                detail::checked_array_size(new_capacity);
            if(requested_capacity <= capacity_)
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
            for(uint32_t idx = 0; idx < size_; ++idx)
            {
                new(new_data + idx) T((*this)[idx]);
            }

            Value old_backing = backing_;
            if(old_backing != Value::None())
            {
                object_clear_value_ownership(old_backing.get_ptr<Object>());
            }
            backing_ = incref(Value::from_oop(new_backing));
            capacity_ = requested_capacity;
            decref(old_backing);
        }

        void resize(size_t new_size)
        {
            static_assert(std::is_default_constructible_v<T>);
            resize(new_size, T());
        }

        void resize(size_t new_size, const T &value)
        {
            uint32_t requested_size = detail::checked_array_size(new_size);
            if(requested_size < size_)
            {
                clear_elements(requested_size, size_);
                size_ = requested_size;
                return;
            }

            reserve(requested_size);
            while(size_ < requested_size)
            {
                T *slot = data() + size_;
                new(slot) T(value);
                incref_element(slot);
                ++size_;
            }
        }

        template <typename... Args> T &emplace_back(Args &&...args)
        {
            if(size_ == capacity_)
            {
                reserve(detail::grown_capacity(capacity_, size_ + 1));
            }

            T *slot = data() + size_;
            new(slot) T(std::forward<Args>(args)...);
            incref_element(slot);
            ++size_;
            return back();
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

        void clear_elements(uint32_t start_idx, uint32_t end_idx)
        {
            if(backing_ == Value::None())
            {
                return;
            }

            for(uint32_t idx = start_idx; idx < end_idx; ++idx)
            {
                clear_element(data() + idx);
            }
        }

        Backing *backing_ptr() { return backing_.get_ptr<Backing>(); }
        const Backing *backing_ptr() const
        {
            return backing_.get_ptr<Backing>();
        }

        Value backing_ = Value::None();
        uint32_t size_ = 0;
        uint32_t capacity_ = 0;
    };

}  // namespace cl

#endif  // CL_VM_ARRAY_H
