#ifndef CL_TYPED_VALUE_H
#define CL_TYPED_VALUE_H

#include "object_model/value.h"
#include "util/result.h"
#include <cassert>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace cl
{
    struct SMI;
    struct CLInt;
    struct None;
    struct Bool;

    [[nodiscard]] Value
    set_pending_invalid_typed_value_error(NativeLayoutId native_layout);
    [[nodiscard]] Value
    set_pending_invalid_typed_value_error(const wchar_t *target_type_name);
    [[nodiscard]] Value raise_exception_for_expected(const wchar_t *type_name,
                                                     const wchar_t *message);
    [[nodiscard]] Value propagate_exception_for_expected();

    template <typename T>
    concept HasRawValueConcept =
        requires(const T &value) { value.raw_value(); };

    template <typename T>
    struct HasRawValue : std::bool_constant<HasRawValueConcept<T>>
    {
    };

    template <typename T>
    concept IsValueLikeConcept =
        requires(Value value) { std::decay_t<T>::from_value_unchecked(value); };

    template <typename T>
    struct IsValueLike : std::bool_constant<IsValueLikeConcept<T>>
    {
    };

    // Expected<T> is split by storage representation. Value-like payloads can
    // use Value::exception_marker() as an in-band exception niche; ordinary C++
    // payloads need an explicit success flag and manually managed storage.
    template <typename T, bool = IsValueLike<T>::value>
    class [[nodiscard]] Expected;

    template <typename T>
    concept HasSemanticTypeConcept =
        requires { typename std::decay_t<T>::semantic_type; };

    template <typename T>
    struct HasSemanticType : std::bool_constant<HasSemanticTypeConcept<T>>
    {
    };

    template <typename A, typename B>
    struct HasSameSemanticType : std::false_type
    {
    };

    template <typename A, typename B>
    requires(HasSemanticType<A>::value && HasSemanticType<B>::value)
    struct HasSameSemanticType<A, B>
        : std::bool_constant<
              std::is_same_v<typename std::decay_t<A>::semantic_type,
                             typename std::decay_t<B>::semantic_type>>
    {
    };

    template <typename A, typename B>
    requires(HasRawValue<A>::value && HasRawValue<B>::value &&
             HasSameSemanticType<A, B>::value &&
             !(std::is_same_v<std::decay_t<A>, Value> &&
               std::is_same_v<std::decay_t<B>, Value>))
    bool operator==(const A &left, const B &right)
    {
        return left.raw_value() == right.raw_value();
    }

    template <typename A, typename B>
    requires(HasRawValue<A>::value && HasRawValue<B>::value &&
             HasSameSemanticType<A, B>::value &&
             !(std::is_same_v<std::decay_t<A>, Value> &&
               std::is_same_v<std::decay_t<B>, Value>))
    bool operator!=(const A &left, const B &right)
    {
        return left.raw_value() != right.raw_value();
    }

    template <typename T> struct TValueTraits;

    template <typename T>
    requires(HasNativeLayoutId<T>::value)
    struct TValueTraits<T>
    {
        using extract_type = T *;

        static bool is_instance(Value value)
        {
            return value.is_ptr() &&
                   value.get_ptr<Object>()->native_layout_id() ==
                       T::native_layout;
        }

        static extract_type extract_unchecked(Value value)
        {
            return value.get_ptr<T>();
        }
    };

    template <> struct TValueTraits<SMI>
    {
        using extract_type = int64_t;

        static bool is_instance(Value value) { return value.is_smi(); }
        static extract_type extract_unchecked(Value value)
        {
            return value.get_smi();
        }
        static const wchar_t *target_type_name() { return L"SMI"; }
    };

    template <> struct TValueTraits<CLInt>
    {
        using extract_type = Value;

        static bool is_instance(Value value) { return value.is_integer(); }
        static extract_type extract_unchecked(Value value) { return value; }
        static const wchar_t *target_type_name() { return L"int"; }
    };

    template <> struct TValueTraits<None>
    {
        using extract_type = void;

        static bool is_instance(Value value) { return value.is_none(); }
        static void extract_unchecked(Value value) { assert(value.is_none()); }
        static const wchar_t *target_type_name() { return L"None"; }
    };

    template <> struct TValueTraits<Bool>
    {
        using extract_type = bool;

        static bool is_instance(Value value) { return value.is_bool(); }
        static extract_type extract_unchecked(Value value)
        {
            assert(value.is_bool());
            return value == Value::True();
        }
        static const wchar_t *target_type_name() { return L"bool"; }
    };

    template <typename T> class TValue
    {
    public:
        using semantic_type = T;

        static Expected<TValue<T>, true> from_value_checked(Value value);
        static Expected<TValue<T>, true>
        from_value_or_raise(Value value, const wchar_t *type_name,
                            const wchar_t *message);
        static TValue from_value_assumed(Value value)
        {
            assert(TValueTraits<T>::is_instance(value));
            return from_value_unchecked(value);
        }

        static TValue from_value_unchecked(Value value)
        {
            return TValue(value);
        }

        static TValue from_smi(int64_t value)
        requires(std::is_same_v<T, SMI> || std::is_same_v<T, CLInt>)
        {
            return from_value_unchecked(Value::from_smi(value));
        }

        static TValue None()
        requires(std::is_same_v<T, None>)
        {
            return from_value_unchecked(Value::None());
        }

        static TValue from_bool(bool value)
        requires(std::is_same_v<T, Bool>)
        {
            return from_value_unchecked(value ? Value::True() : Value::False());
        }

        static TValue True()
        requires(std::is_same_v<T, Bool>)
        {
            return from_value_unchecked(Value::True());
        }

        static TValue False()
        requires(std::is_same_v<T, Bool>)
        {
            return from_value_unchecked(Value::False());
        }

        template <typename U = T,
                  typename ExtractType = typename TValueTraits<U>::extract_type>
        requires(std::is_pointer_v<ExtractType>)
        static TValue from_oop(std::remove_pointer_t<ExtractType> *ptr)
        {
            return from_value_unchecked(Value::from_oop(ptr));
        }

        template <typename U = T,
                  typename ExtractType = typename TValueTraits<U>::extract_type>
        ExtractType extract() const
        {
            return TValueTraits<U>::extract_unchecked(value_);
        }

        Value raw_value() const { return value_; }

    private:
        explicit TValue(Value value) : value_(value) {}

        Value value_;
    };

    template <typename T> class Optional
    {
        struct RawValueTag
        {
        };

    public:
        using semantic_type = typename T::semantic_type;

        static_assert(!std::is_same_v<semantic_type, None>);

        Optional() : value_(Value::None()) {}
        explicit Optional(T value) : value_(value.raw_value())
        {
            assert(!value_.is_none());
        }

        static Optional none()
        {
            return Optional(RawValueTag{}, Value::None());
        }

        static Optional some(T value) { return Optional(value); }

        static Optional from_value_unchecked(Value value)
        {
            return Optional(RawValueTag{}, value);
        }

        bool has_value() const { return !value_.is_none(); }
        bool is_none() const { return value_.is_none(); }
        explicit operator bool() const { return has_value(); }

        T value() const
        {
            assert(has_value());
            return T::from_value_unchecked(value_);
        }
        T operator*() const { return value(); }

        Value raw_value() const { return value_; }

    private:
        Optional(RawValueTag, Value value) : value_(value) {}

        Value value_;
    };

    // Compact Expected<T> representation for Value and TValue-like handles.
    // The pending-exception state is carried by Value::exception_marker().
    template <typename T> class [[nodiscard]] Expected<T, true>
    {
    public:
        using semantic_type = typename T::semantic_type;

        Expected() = delete;
        explicit Expected(T value) : value_(value.raw_value())
        {
            assert(!value_.raw_value().is_exception_marker());
        }

        [[nodiscard]] static Expected ok(T value) { return Expected(value); }

        [[nodiscard]] static Expected raise_exception(const wchar_t *type_name,
                                                      const wchar_t *message)
        {
            return Expected(RawValueTag{},
                            raise_exception_for_expected(type_name, message));
        }

        [[nodiscard]] static Expected propagate_exception()
        {
            return Expected(RawValueTag{}, propagate_exception_for_expected());
        }

        [[nodiscard]] static Expected from_value_unchecked(Value value)
        {
            return Expected(RawValueTag{}, value);
        }

        bool has_value() const { return !value_.is_exception_marker(); }
        bool has_exception() const { return value_.is_exception_marker(); }
        explicit operator bool() const { return has_value(); }

        T value() const
        {
            assert(has_value());
            return T::from_value_unchecked(value_);
        }
        T operator*() const { return value(); }

        Value raw_value() const { return value_; }

    private:
        struct RawValueTag
        {
        };

        Expected(RawValueTag, Value value) : value_(value) {}

        Value value_;
    };

    // General Expected<T> representation for non-Value payloads, such as AST
    // indexes. This deliberately uses a bool word instead of sentinel values.
    template <typename T> class [[nodiscard]] Expected<T, false>
    {
    public:
        using semantic_type = T;

        Expected() = delete;

        explicit Expected(T value) : has_value_(true)
        {
            std::construct_at(value_ptr(), std::move(value));
        }

        Expected(const Expected &other) : has_value_(other.has_value_)
        {
            if(has_value_)
            {
                std::construct_at(value_ptr(), other.value_ref());
            }
        }

        Expected(Expected &&other) noexcept(
            std::is_nothrow_move_constructible_v<T>)
            : has_value_(other.has_value_)
        {
            if(has_value_)
            {
                std::construct_at(value_ptr(), std::move(other.value_ref()));
            }
        }

        Expected &operator=(const Expected &other)
        {
            if(this == &other)
            {
                return *this;
            }
            if(has_value_ && other.has_value_)
            {
                value_ref() = other.value_ref();
            }
            else if(has_value_)
            {
                destroy_value();
                has_value_ = false;
            }
            else if(other.has_value_)
            {
                std::construct_at(value_ptr(), other.value_ref());
                has_value_ = true;
            }
            return *this;
        }

        Expected &operator=(Expected &&other) noexcept(
            std::is_nothrow_move_constructible_v<T> &&
            std::is_nothrow_move_assignable_v<T>)
        {
            if(this == &other)
            {
                return *this;
            }
            if(has_value_ && other.has_value_)
            {
                value_ref() = std::move(other.value_ref());
            }
            else if(has_value_)
            {
                destroy_value();
                has_value_ = false;
            }
            else if(other.has_value_)
            {
                std::construct_at(value_ptr(), std::move(other.value_ref()));
                has_value_ = true;
            }
            return *this;
        }

        ~Expected()
        {
            if(has_value_)
            {
                destroy_value();
            }
        }

        [[nodiscard]] static Expected ok(T value)
        {
            return Expected(std::move(value));
        }

        [[nodiscard]] static Expected raise_exception(const wchar_t *type_name,
                                                      const wchar_t *message)
        {
            return Expected(ErrorTag{},
                            raise_exception_for_expected(type_name, message));
        }

        [[nodiscard]] static Expected propagate_exception()
        {
            return Expected(ErrorTag{}, propagate_exception_for_expected());
        }

        bool has_value() const { return has_value_; }
        bool has_exception() const { return !has_value_; }
        explicit operator bool() const { return has_value(); }

        T value() const &
        {
            assert(has_value());
            return value_ref();
        }

        T value() &&
        {
            assert(has_value());
            return std::move(value_ref());
        }

        T operator*() const { return value(); }

    private:
        struct ErrorTag
        {
        };

        Expected(ErrorTag, Value value) : has_value_(false)
        {
            assert(value.is_exception_marker());
        }

        T *value_ptr()
        {
            return std::launder(reinterpret_cast<T *>(&storage_));
        }

        const T *value_ptr() const
        {
            return std::launder(reinterpret_cast<const T *>(&storage_));
        }

        T &value_ref() { return *value_ptr(); }

        const T &value_ref() const { return *value_ptr(); }

        void destroy_value() { std::destroy_at(value_ptr()); }

        bool has_value_;
        std::aligned_storage_t<sizeof(T), alignof(T)> storage_;
    };

    template <> class [[nodiscard]] Expected<void, false>
    {
    public:
        Expected() = delete;

        [[nodiscard]] static Expected ok() { return Expected(true); }

        [[nodiscard]] static Expected raise_exception(const wchar_t *type_name,
                                                      const wchar_t *message)
        {
            return Expected(ErrorTag{},
                            raise_exception_for_expected(type_name, message));
        }

        [[nodiscard]] static Expected propagate_exception()
        {
            return Expected(ErrorTag{}, propagate_exception_for_expected());
        }

        bool has_value() const { return has_value_; }
        bool has_exception() const { return !has_value_; }
        explicit operator bool() const { return has_value(); }

        void value() const { assert(has_value()); }

    private:
        struct ErrorTag
        {
        };

        explicit Expected(bool has_value) : has_value_(has_value) {}

        Expected(ErrorTag, Value value) : has_value_(false)
        {
            assert(value.is_exception_marker());
        }

        bool has_value_;
    };

    class PropagatedException
    {
    public:
        // Propagates an already-pending exception through APIs that return
        // either a raw Value exception marker or an Expected<T> error state.
        PropagatedException();

        operator Value() const { return Value::exception_marker(); }

        template <typename T> operator Expected<T>() const
        {
            return Expected<T>::propagate_exception();
        }
    };

    template <typename T, bool IsValueLikePayload>
    PropagatedException
    propagate_failure(Expected<T, IsValueLikePayload> &&expected)
    {
        assert(expected.has_exception());
        return PropagatedException();
    }

    static_assert(sizeof(Optional<Value>) == sizeof(Value));
    static_assert(sizeof(Expected<Value>) == sizeof(Value));

    template <typename T>
    Expected<TValue<T>, true> TValue<T>::from_value_checked(Value value)
    {
        if(!TValueTraits<T>::is_instance(value))
        {
            if constexpr(HasNativeLayoutId<T>::value)
            {
                (void)set_pending_invalid_typed_value_error(T::native_layout);
            }
            else
            {
                (void)set_pending_invalid_typed_value_error(
                    TValueTraits<T>::target_type_name());
            }
            return Expected<TValue<T>>::propagate_exception();
        }
        return Expected<TValue<T>>::ok(from_value_unchecked(value));
    }

    template <typename T>
    Expected<TValue<T>, true>
    TValue<T>::from_value_or_raise(Value value, const wchar_t *type_name,
                                   const wchar_t *message)
    {
        if(!TValueTraits<T>::is_instance(value))
        {
            return Expected<TValue<T>>::raise_exception(type_name, message);
        }
        return Expected<TValue<T>>::ok(from_value_unchecked(value));
    }

}  // namespace cl

#endif  // CL_TYPED_VALUE_H
