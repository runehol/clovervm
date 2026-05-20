#ifndef CL_TYPED_VALUE_H
#define CL_TYPED_VALUE_H

#include "value.h"
#include <cassert>
#include <type_traits>

namespace cl
{
    struct SMI;
    struct CLInt;
    struct None;
    struct Bool;

    template <typename T> class Expected;

    [[nodiscard]] Value
    set_pending_invalid_typed_value_error(NativeLayoutId native_layout);
    [[nodiscard]] Value
    set_pending_invalid_typed_value_error(const wchar_t *target_type_name);
    [[nodiscard]] Value raise_exception_for_expected(const wchar_t *type_name,
                                                     const wchar_t *message);
    [[nodiscard]] Value propagate_exception_for_expected();

    template <typename T, typename = void> struct HasRawValue : std::false_type
    {
    };

    template <typename T>
    struct HasRawValue<
        T, std::void_t<decltype(std::declval<const T &>().raw_value())>>
        : std::true_type
    {
    };

    template <typename T, typename = void>
    struct HasSemanticType : std::false_type
    {
    };

    template <typename T>
    struct HasSemanticType<T,
                           std::void_t<typename std::decay_t<T>::semantic_type>>
        : std::true_type
    {
    };

    template <typename A, typename B, typename = void>
    struct HasSameSemanticType : std::false_type
    {
    };

    template <typename A, typename B>
    struct HasSameSemanticType<A, B,
                               std::enable_if_t<HasSemanticType<A>::value &&
                                                HasSemanticType<B>::value>>
        : std::bool_constant<
              std::is_same_v<typename std::decay_t<A>::semantic_type,
                             typename std::decay_t<B>::semantic_type>>
    {
    };

    template <typename A, typename B,
              std::enable_if_t<HasRawValue<A>::value && HasRawValue<B>::value &&
                                   HasSameSemanticType<A, B>::value &&
                                   !(std::is_same_v<std::decay_t<A>, Value> &&
                                     std::is_same_v<std::decay_t<B>, Value>),
                               int> = 0>
    bool operator==(const A &left, const B &right)
    {
        return left.raw_value() == right.raw_value();
    }

    template <typename A, typename B,
              std::enable_if_t<HasRawValue<A>::value && HasRawValue<B>::value &&
                                   HasSameSemanticType<A, B>::value &&
                                   !(std::is_same_v<std::decay_t<A>, Value> &&
                                     std::is_same_v<std::decay_t<B>, Value>),
                               int> = 0>
    bool operator!=(const A &left, const B &right)
    {
        return left.raw_value() != right.raw_value();
    }

    template <typename T, typename Enable = void> struct TValueTraits;

    template <typename T>
    struct TValueTraits<T, std::enable_if_t<HasNativeLayoutId<T>::value>>
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

        static Expected<TValue<T>> from_value_checked(Value value);
        static Expected<TValue<T>> from_value_or_raise(Value value,
                                                       const wchar_t *type_name,
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

        template <typename U = T,
                  typename = std::enable_if_t<std::is_same_v<U, SMI> ||
                                              std::is_same_v<U, CLInt>>>
        static TValue from_smi(int64_t value)
        {
            return from_value_unchecked(Value::from_smi(value));
        }

        template <typename U = T,
                  typename = std::enable_if_t<std::is_same_v<U, None>>>
        static TValue None()
        {
            return from_value_unchecked(Value::None());
        }

        template <typename U = T,
                  typename = std::enable_if_t<std::is_same_v<U, Bool>>>
        static TValue from_bool(bool value)
        {
            return from_value_unchecked(value ? Value::True() : Value::False());
        }

        template <typename U = T,
                  typename = std::enable_if_t<std::is_same_v<U, Bool>>>
        static TValue True()
        {
            return from_value_unchecked(Value::True());
        }

        template <typename U = T,
                  typename = std::enable_if_t<std::is_same_v<U, Bool>>>
        static TValue False()
        {
            return from_value_unchecked(Value::False());
        }

        template <typename U = T,
                  typename ExtractType = typename TValueTraits<U>::extract_type,
                  typename = std::enable_if_t<std::is_pointer_v<ExtractType>>>
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

    template <typename T> class Expected
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

    static_assert(sizeof(Optional<Value>) == sizeof(Value));
    static_assert(sizeof(Expected<Value>) == sizeof(Value));

    template <typename T>
    Expected<TValue<T>> TValue<T>::from_value_checked(Value value)
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
    Expected<TValue<T>> TValue<T>::from_value_or_raise(Value value,
                                                       const wchar_t *type_name,
                                                       const wchar_t *message)
    {
        if(!TValueTraits<T>::is_instance(value))
        {
            return Expected<TValue<T>>::raise_exception(type_name, message);
        }
        return Expected<TValue<T>>::ok(from_value_unchecked(value));
    }

}  // namespace cl

// Unwrap an Expected-like result or propagate its pending exception from the
// enclosing function. The enclosing function must return either Value or
// Expected<T>.
#define CL_TRY(expr)                                                           \
    ({                                                                         \
        auto cl_try_result = (expr);                                           \
        if(unlikely(!cl_try_result))                                           \
        {                                                                      \
            return ::cl::PropagatedException();                                \
        }                                                                      \
        cl_try_result.value();                                                 \
    })

#endif  // CL_TYPED_VALUE_H
