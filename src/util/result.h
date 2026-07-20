#ifndef CL_UTIL_RESULT_H
#define CL_UTIL_RESULT_H

#include "util/compiler.h"
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

namespace cl
{
    template <typename T, typename Error> class Result;

    template <typename Error> class [[nodiscard]] Unexpected
    {
    public:
        explicit Unexpected(Error error) : error_(std::move(error)) {}

        Unexpected(const Unexpected &) = delete;
        Unexpected &operator=(const Unexpected &) = delete;
        Unexpected(Unexpected &&) = default;
        Unexpected &operator=(Unexpected &&) = default;

        template <typename T> operator Result<T, Error>() &&
        {
            return Result<T, Error>::error(std::move(error_));
        }

    private:
        Error error_;
    };

    template <typename T, typename Error> class [[nodiscard]] Result
    {
        static_assert(!std::is_void_v<T>);
        static_assert(!std::is_reference_v<T>);
        static_assert(!std::is_reference_v<Error>);

    public:
        Result() = delete;

        [[nodiscard]] static Result ok(T value)
        {
            return Result(ValueTag{}, std::move(value));
        }

        [[nodiscard]] static Result error(Error error)
        {
            return Result(ErrorTag{}, std::move(error));
        }

        bool has_value() const { return storage_.index() == ValueIndex; }
        bool has_error() const { return storage_.index() == ErrorIndex; }
        explicit operator bool() const { return has_value(); }

        T &value() &
        {
            assert(has_value());
            return std::get<ValueIndex>(storage_);
        }

        const T &value() const &
        {
            assert(has_value());
            return std::get<ValueIndex>(storage_);
        }

        T value() &&
        {
            assert(has_value());
            return std::move(std::get<ValueIndex>(storage_));
        }

        Error &error() &
        {
            assert(has_error());
            return std::get<ErrorIndex>(storage_);
        }

        const Error &error() const &
        {
            assert(has_error());
            return std::get<ErrorIndex>(storage_);
        }

        Error error() &&
        {
            assert(has_error());
            return std::move(std::get<ErrorIndex>(storage_));
        }

        T &operator*() & { return value(); }
        const T &operator*() const & { return value(); }

    private:
        struct ValueTag
        {
        };

        struct ErrorTag
        {
        };

        static constexpr size_t ValueIndex = 0;
        static constexpr size_t ErrorIndex = 1;

        Result(ValueTag, T value)
            : storage_(std::in_place_index<ValueIndex>, std::move(value))
        {
        }

        Result(ErrorTag, Error error)
            : storage_(std::in_place_index<ErrorIndex>, std::move(error))
        {
        }

        std::variant<T, Error> storage_;
    };

    template <typename Error> class [[nodiscard]] Result<void, Error>
    {
        static_assert(!std::is_reference_v<Error>);

    public:
        Result() = delete;

        [[nodiscard]] static Result ok() { return Result(ValueTag{}); }

        [[nodiscard]] static Result error(Error error)
        {
            return Result(ErrorTag{}, std::move(error));
        }

        bool has_value() const { return storage_.index() == ValueIndex; }
        bool has_error() const { return storage_.index() == ErrorIndex; }
        explicit operator bool() const { return has_value(); }

        void value() const { assert(has_value()); }

        Error &error() &
        {
            assert(has_error());
            return std::get<ErrorIndex>(storage_);
        }

        const Error &error() const &
        {
            assert(has_error());
            return std::get<ErrorIndex>(storage_);
        }

        Error error() &&
        {
            assert(has_error());
            return std::move(std::get<ErrorIndex>(storage_));
        }

    private:
        struct ValueTag
        {
        };

        struct ErrorTag
        {
        };

        static constexpr size_t ValueIndex = 0;
        static constexpr size_t ErrorIndex = 1;

        explicit Result(ValueTag) : storage_(std::in_place_index<ValueIndex>) {}

        Result(ErrorTag, Error error)
            : storage_(std::in_place_index<ErrorIndex>, std::move(error))
        {
        }

        std::variant<std::monostate, Error> storage_;
    };

    template <typename T, typename Error>
    Unexpected<Error> propagate_failure(Result<T, Error> &&result)
    {
        assert(result.has_error());
        return Unexpected<Error>(std::move(result).error());
    }
}  // namespace cl

// Unwrap a Result- or Expected-like value, or propagate its error from the
// enclosing function through that type's cl::propagate_failure overload.
#define CL_TRY(expr)                                                           \
    ({                                                                         \
        auto cl_try_result = (expr);                                           \
        if(unlikely(!static_cast<bool>(cl_try_result)))                        \
        {                                                                      \
            return ::cl::propagate_failure(std::move(cl_try_result));          \
        }                                                                      \
        std::move(cl_try_result).value();                                      \
    })

#endif  // CL_UTIL_RESULT_H
