#include "exception_object.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "typed_value.h"
#include "value_state.h"
#include <gtest/gtest.h>
#include <type_traits>

using namespace cl;

namespace
{
    static void expect_pending_exception(ThreadState *thread,
                                         const wchar_t *class_name,
                                         const wchar_t *message)
    {
        ASSERT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        TValue2<Exception> exception = thread->pending_exception_object();
        EXPECT_STREQ(class_name, exception.extract()
                                     ->get_shape()
                                     ->get_class()
                                     ->get_name()
                                     .extract()
                                     ->data);
        EXPECT_STREQ(message, exception.extract()->message.extract()->data);
    }

    static Value require_smi_for_test(Value value)
    {
        TValue2<SMI> smi = CL_TRY(TValue2<SMI>::from_value_or_raise(
            value, L"TypeError", L"test expected an SMI"));
        return smi.raw_value();
    }

    static Expected<TValue2<Bool>>
    require_smi_then_return_bool_for_test(Value value)
    {
        (void)CL_TRY(TValue2<SMI>::from_value_or_raise(
            value, L"TypeError", L"test expected an SMI"));
        return Expected<TValue2<Bool>>::ok(TValue2<Bool>::True());
    }
}  // namespace

TEST(TValue2, SmiUsesSameBasicProtocolAsTValue)
{
    TValue2<SMI> smi = TValue2<SMI>::from_value_unchecked(Value::from_smi(42));

    static_assert(std::is_same_v<TValue2<SMI>::semantic_type, SMI>);
    static_assert(!std::is_default_constructible_v<TValue2<SMI>>);

    EXPECT_EQ(Value::from_smi(42), smi.raw_value());
}

TEST(TValue2, SmiFactoryAndExtractUseTrait)
{
    TValue2<SMI> smi = TValue2<SMI>::from_smi(42);

    EXPECT_EQ(Value::from_smi(42), smi.raw_value());
    EXPECT_EQ(42, smi.extract());
}

TEST(TValue2, StringRoundTripsObjectPointer)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"v2");
    TValue2<String> typed_string =
        TValue2<String>::from_value_unchecked(Value::from_oop(string));

    EXPECT_EQ(Value::from_oop(string), typed_string.raw_value());
}

TEST(TValue2, OopFactoryAndExtractUseNativeLayoutTrait)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"v2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    EXPECT_EQ(Value::from_oop(string), typed_string.raw_value());
    EXPECT_EQ(string, typed_string.extract());
}

TEST(TValue2, CheckedConstructionReturnsExpectedSuccess)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"v2");
    Expected<TValue2<String>> typed_string =
        TValue2<String>::from_value_checked(Value::from_oop(string));

    ASSERT_TRUE(typed_string.has_value());
    EXPECT_EQ(string, typed_string.value().extract());
    EXPECT_FALSE(context.thread()->has_pending_exception());
}

TEST(TValue2, CheckedConstructionSetsTypeErrorOnFailure)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<String>> typed_string =
        TValue2<String>::from_value_checked(Value::from_smi(42));

    EXPECT_TRUE(typed_string.has_exception());
    EXPECT_TRUE(typed_string.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"invalid typed value construction for str");
}

TEST(TValue2, CheckedSmiConstructionNamesTargetTypeOnFailure)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<SMI>> smi =
        TValue2<SMI>::from_value_checked(Value::True());

    EXPECT_TRUE(smi.has_exception());
    EXPECT_TRUE(smi.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"invalid typed value construction for SMI");
}

TEST(TValue2, NoneFactoryAndCheckedConstructionUseNoneSingleton)
{
    TValue2<None> none = TValue2<None>::None();

    static_assert(std::is_same_v<TValue2<None>::semantic_type, None>);

    EXPECT_EQ(Value::None(), none.raw_value());

    Expected<TValue2<None>> checked =
        TValue2<None>::from_value_checked(Value::None());
    ASSERT_TRUE(checked.has_value());
    EXPECT_EQ(Value::None(), checked.value().raw_value());
}

TEST(TValue2, CheckedNoneConstructionNamesTargetTypeOnFailure)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<None>> none =
        TValue2<None>::from_value_checked(Value::from_smi(42));

    EXPECT_TRUE(none.has_exception());
    EXPECT_TRUE(none.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"invalid typed value construction for None");
}

TEST(TValue2, BoolFactoriesAndExtractUseBoolSingletons)
{
    TValue2<Bool> true_value = TValue2<Bool>::True();
    TValue2<Bool> false_value = TValue2<Bool>::False();
    TValue2<Bool> from_bool = TValue2<Bool>::from_bool(true);

    static_assert(std::is_same_v<TValue2<Bool>::semantic_type, Bool>);

    EXPECT_EQ(Value::True(), true_value.raw_value());
    EXPECT_EQ(Value::False(), false_value.raw_value());
    EXPECT_EQ(Value::True(), from_bool.raw_value());
    EXPECT_TRUE(true_value.extract());
    EXPECT_FALSE(false_value.extract());
}

TEST(TValue2, CheckedBoolConstructionNamesTargetTypeOnFailure)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<Bool>> boolean =
        TValue2<Bool>::from_value_checked(Value::None());

    EXPECT_TRUE(boolean.has_exception());
    EXPECT_TRUE(boolean.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"invalid typed value construction for bool");
}

TEST(TValue2, CustomCheckedConstructionReturnsExpectedSuccess)
{
    Expected<TValue2<SMI>> smi = TValue2<SMI>::from_value_or_raise(
        Value::from_smi(42), L"TypeError", L"expected an SMI");

    ASSERT_TRUE(smi.has_value());
    EXPECT_EQ(42, smi.value().extract());
}

TEST(TValue2, CustomCheckedConstructionRaisesSuppliedExceptionOnFailure)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<SMI>> smi = TValue2<SMI>::from_value_or_raise(
        Value::None(), L"TypeError", L"expected an SMI");

    EXPECT_TRUE(smi.has_exception());
    EXPECT_TRUE(smi.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"expected an SMI");
}

TEST(TValue2, AssumedConstructionAssertsTypeAndReturnsTypedValue)
{
    TValue2<SMI> smi = TValue2<SMI>::from_value_assumed(Value::from_smi(42));

    EXPECT_EQ(42, smi.extract());
}

TEST(Expected, TryMacroUnwrapsSuccess)
{
    EXPECT_EQ(Value::from_smi(42), require_smi_for_test(Value::from_smi(42)));
}

TEST(Expected, TryMacroPropagatesExceptionMarker)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value result = require_smi_for_test(Value::None());

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"test expected an SMI");
}

TEST(Expected, TryMacroPropagatesIntoDifferentExpectedPayload)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<Bool>> result =
        require_smi_then_return_bool_for_test(Value::None());

    EXPECT_TRUE(result.has_exception());
    EXPECT_TRUE(result.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"test expected an SMI");
}

TEST(Expected, TryMacroUnwrapsSuccessInExpectedReturningFunction)
{
    Expected<TValue2<Bool>> result =
        require_smi_then_return_bool_for_test(Value::from_smi(42));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().extract());
}

TEST(Expected, ValueBackedSuccessUsesRawValueRepresentation)
{
    Expected<Value> expected(Value::from_smi(42));

    EXPECT_TRUE(expected.has_value());
    EXPECT_FALSE(expected.has_exception());
    EXPECT_TRUE(expected);
    EXPECT_EQ(Value::from_smi(42), expected.value());
    EXPECT_EQ(Value::from_smi(42), *expected);
    EXPECT_EQ(Value::from_smi(42), expected.raw_value());
}

TEST(Expected, OkConstructorMatchesValueConstructor)
{
    Expected<Value> expected = Expected<Value>::ok(Value::from_smi(42));

    EXPECT_TRUE(expected.has_value());
    EXPECT_EQ(Value::from_smi(42), expected.value());
}

TEST(Expected, ValueBackedExceptionUsesExceptionMarkerNiche)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<Value> expected =
        Expected<Value>::raise_exception(L"TypeError", L"bad value");

    EXPECT_FALSE(expected.has_value());
    EXPECT_TRUE(expected.has_exception());
    EXPECT_FALSE(expected);
    EXPECT_TRUE(expected.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError", L"bad value");
}

TEST(Expected, PropagatedExceptionCarriesExistingExceptionMarker)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    (void)context.thread()->set_pending_builtin_exception_string(
        L"TypeError", L"already pending");

    Expected<Value> expected = Expected<Value>::propagate_exception();

    EXPECT_TRUE(expected.has_exception());
    EXPECT_TRUE(expected.raw_value().is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"already pending");
}

TEST(Expected, RecursesSemanticTypeThroughTypedValue)
{
    using ExpectedSmi = Expected<TValue<SMI>>;

    static_assert(std::is_same_v<ExpectedSmi::semantic_type, SMI>);

    ExpectedSmi expected = ExpectedSmi::ok(TValue<SMI>::from_smi(42));

    EXPECT_TRUE(expected.has_value());
    EXPECT_EQ(Value::from_smi(42), expected.raw_value());
    EXPECT_EQ(42, expected.value().extract());
    EXPECT_EQ(42, (*expected).extract());
}

TEST(Expected, NoneTypedValueRepresentsNoneOrException)
{
    using ExpectedNone = Expected<TValue2<None>>;

    static_assert(sizeof(ExpectedNone) == sizeof(Value));
    static_assert(std::is_same_v<ExpectedNone::semantic_type, None>);

    ExpectedNone success = ExpectedNone::ok(TValue2<None>::None());

    EXPECT_TRUE(success.has_value());
    EXPECT_EQ(Value::None(), success.value().raw_value());
    EXPECT_EQ(Value::None(), success.raw_value());
}

TEST(Optional, ValueBackedNoneUsesNoneNiche)
{
    Optional<Value> optional = Optional<Value>::none();

    EXPECT_FALSE(optional.has_value());
    EXPECT_TRUE(optional.is_none());
    EXPECT_FALSE(optional);
    EXPECT_EQ(Value::None(), optional.raw_value());
}

TEST(Optional, ConstructsSomeValueAndUnpacksLikeStdOptional)
{
    Optional<Value> optional(Value::from_smi(42));

    EXPECT_TRUE(optional.has_value());
    EXPECT_FALSE(optional.is_none());
    EXPECT_TRUE(optional);
    EXPECT_EQ(Value::from_smi(42), optional.value());
    EXPECT_EQ(Value::from_smi(42), *optional);
    EXPECT_EQ(Value::from_smi(42), optional.raw_value());
}

TEST(Optional, TypedValueUnpacksThroughFromValueUnchecked)
{
    Optional<TValue<SMI>> optional(TValue<SMI>::from_smi(42));

    EXPECT_TRUE(optional.has_value());
    EXPECT_EQ(Value::from_smi(42), optional.raw_value());
    EXPECT_EQ(42, optional.value().extract());
    EXPECT_EQ(42, (*optional).extract());
}

TEST(Optional, RecursesSemanticTypeThroughExpectedAndTypedValue)
{
    using MaybeString = Optional<Expected<TValue<String>>>;

    static_assert(std::is_same_v<MaybeString::semantic_type, String>);
}
