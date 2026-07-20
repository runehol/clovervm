#include "runtime/thread_state.h"
#include "test_helpers.h"
#include "util/result.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using namespace cl;

namespace
{
    enum class TestError
    {
        First,
        Second,
    };

    static Result<int, TestError> integer_result(bool succeed)
    {
        if(!succeed)
        {
            return Result<int, TestError>::error(TestError::First);
        }
        return Result<int, TestError>::ok(42);
    }

    static Result<std::string, TestError> convert_result(bool succeed)
    {
        int value = CL_TRY(integer_result(succeed));
        return Result<std::string, TestError>::ok(std::to_string(value));
    }

    static Result<void, TestError> void_result(bool succeed)
    {
        (void)CL_TRY(integer_result(succeed));
        return Result<void, TestError>::ok();
    }

    static Result<int, std::unique_ptr<int>> move_only_error_result()
    {
        return Result<int, std::unique_ptr<int>>::error(
            std::make_unique<int>(17));
    }

    static Result<void, std::unique_ptr<int>> propagate_move_only_error()
    {
        (void)CL_TRY(move_only_error_result());
        return Result<void, std::unique_ptr<int>>::ok();
    }
}  // namespace

TEST(Result, StoresValueOrError)
{
    static_assert(!std::is_default_constructible_v<Result<int, TestError>>);

    Result<int, TestError> success = Result<int, TestError>::ok(42);
    Result<int, TestError> failure =
        Result<int, TestError>::error(TestError::Second);

    EXPECT_TRUE(success.has_value());
    EXPECT_FALSE(success.has_error());
    EXPECT_TRUE(success);
    EXPECT_EQ(42, success.value());
    EXPECT_EQ(42, *success);

    EXPECT_FALSE(failure.has_value());
    EXPECT_TRUE(failure.has_error());
    EXPECT_FALSE(failure);
    EXPECT_EQ(TestError::Second, failure.error());
}

TEST(Result, TryUnwrapsSuccessAndPropagatesErrorAcrossPayloadTypes)
{
    Result<std::string, TestError> success = convert_result(true);
    Result<std::string, TestError> failure = convert_result(false);

    ASSERT_TRUE(success.has_value());
    EXPECT_EQ("42", success.value());
    ASSERT_TRUE(failure.has_error());
    EXPECT_EQ(TestError::First, failure.error());
}

TEST(Result, DistinguishesValueAndErrorWhenTheirTypesMatch)
{
    Result<int, int> success = Result<int, int>::ok(42);
    Result<int, int> failure = Result<int, int>::error(17);

    ASSERT_TRUE(success.has_value());
    EXPECT_EQ(42, success.value());
    ASSERT_TRUE(failure.has_error());
    EXPECT_EQ(17, failure.error());
}

TEST(Result, VoidResultSupportsSuccessAndPropagation)
{
    Result<void, TestError> success = void_result(true);
    Result<void, TestError> failure = void_result(false);

    EXPECT_TRUE(success.has_value());
    success.value();
    ASSERT_TRUE(failure.has_error());
    EXPECT_EQ(TestError::First, failure.error());
}

TEST(Result, SupportsMoveOnlyValues)
{
    Result<std::unique_ptr<int>, TestError> result =
        Result<std::unique_ptr<int>, TestError>::ok(std::make_unique<int>(42));

    std::unique_ptr<int> value = std::move(result).value();
    ASSERT_NE(nullptr, value);
    EXPECT_EQ(42, *value);
}

TEST(Result, PropagatesMoveOnlyErrors)
{
    Result<void, std::unique_ptr<int>> result = propagate_move_only_error();

    ASSERT_TRUE(result.has_error());
    std::unique_ptr<int> error = std::move(result).error();
    ASSERT_NE(nullptr, error);
    EXPECT_EQ(17, *error);
}

TEST(Result, PropagationDoesNotSetPendingPythonException)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Result<std::string, TestError> result = convert_result(false);

    EXPECT_TRUE(result.has_error());
    EXPECT_FALSE(context.thread()->has_pending_exception());
}
