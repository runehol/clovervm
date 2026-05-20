#include "dict.h"
#include "exception_propagation.h"
#include "owned.h"
#include "scope.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "value_state.h"
#include <gtest/gtest.h>

using namespace cl;

static Value propagate_success_for_test(Value value)
{
    CL_PROPAGATE_EXCEPTION(value);
    return Value::from_smi(42);
}

static Value propagate_expression_once_for_test(int &n_evaluations, Value value)
{
    CL_PROPAGATE_EXCEPTION((++n_evaluations, value));
    return Value::from_smi(42);
}

TEST(ExceptionPropagation, ContinuesForOrdinaryValues)
{
    EXPECT_EQ(Value::from_smi(42),
              propagate_success_for_test(Value::from_smi(1)));
}

TEST(ExceptionPropagation, ReturnsExceptionMarker)
{
    EXPECT_TRUE(propagate_success_for_test(Value::exception_marker())
                    .is_exception_marker());
}

TEST(ExceptionPropagation, EvaluatesExpressionOnce)
{
    int n_evaluations = 0;

    EXPECT_TRUE(propagate_expression_once_for_test(n_evaluations,
                                                   Value::exception_marker())
                    .is_exception_marker());
    EXPECT_EQ(1, n_evaluations);
}

TEST(TValue2, StringAllowsConstructionFromNonInternedString)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"hello");

    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    EXPECT_EQ(string, typed_string.extract());
    EXPECT_STREQ(L"hello", typed_string.extract()->data);
}

TEST(HeapPtr, ScopeUsesDirectHeapPointerHandle)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *scope = context.thread()->make_internal_raw<Scope>(nullptr);

    HeapPtr<Scope> scope_ptr(scope);

    EXPECT_EQ(scope, scope_ptr.get());
    EXPECT_TRUE(scope_ptr.get()->empty());
}

TEST(NativeLayout, ObjectAndValueConversionHelpersUseExactLayout)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"layout");
    Value string_value = Value::from_oop(string);
    Object *object = string_value.get_ptr<Object>();

    EXPECT_EQ(NativeLayoutId::String, object->native_layout_id());
    EXPECT_TRUE(can_convert_to<String>(object));
    EXPECT_FALSE(can_convert_to<Dict>(object));
    EXPECT_EQ(string, try_convert_to<String>(object));
    EXPECT_EQ(nullptr, try_convert_to<Dict>(object));
    EXPECT_EQ(string, assume_convert_to<String>(object));
    EXPECT_TRUE(can_convert_to<String>(string_value));
    EXPECT_EQ(string, try_convert_to<String>(string_value));
    EXPECT_EQ(string, assume_convert_to<String>(string_value));
    EXPECT_EQ(nullptr, try_convert_to<String>(Value::None()));
}

TEST(TValue2, UnsafeUncheckedRoundTripsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"unsafe");
    Value raw = Value::from_oop(string);

    TValue2<String> typed_string = TValue2<String>::from_value_unchecked(raw);

    EXPECT_EQ(raw, typed_string.raw_value());
    EXPECT_EQ(string, typed_string.extract());
}

TEST(TValue2, CheckedConstructionReturnsExceptionOnWrongType)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<String>> result =
        TValue2<String>::from_value_checked(Value::True());

    EXPECT_TRUE(result.has_exception());
    EXPECT_TRUE(result.raw_value().is_exception_marker());
    EXPECT_TRUE(context.thread()->has_pending_exception());
    context.thread()->clear_pending_exception();
}

TEST(TValue2, SmiUsesTraitDefinedGetter)
{
    TValue2<SMI> smi = TValue2<SMI>::from_smi(42);

    EXPECT_EQ(42, smi.extract());
}

TEST(TValue2, ClIntAcceptsCurrentIntegerRepresentation)
{
    TValue2<CLInt> integer = TValue2<CLInt>::from_smi(42);

    EXPECT_EQ(Value::from_smi(42), integer.raw_value());
}

TEST(Value, InlineTypePredicatesDistinguishSmiBoolAndNone)
{
    EXPECT_TRUE(Value::from_smi(0).is_smi());
    EXPECT_TRUE(Value::from_smi(0).is_integer());
    EXPECT_FALSE(Value::from_smi(0).is_bool());
    EXPECT_FALSE(Value::from_smi(0).is_none());

    EXPECT_TRUE(Value::True().is_bool());
    EXPECT_TRUE(Value::False().is_bool());
    EXPECT_FALSE(Value::True().is_smi());
    EXPECT_FALSE(Value::False().is_smi());
    EXPECT_FALSE(Value::True().is_none());
    EXPECT_FALSE(Value::False().is_none());

    EXPECT_TRUE(Value::None().is_none());
    EXPECT_FALSE(Value::None().is_smi());
    EXPECT_FALSE(Value::None().is_bool());
}

TEST(OwnedValueState, RetainsAndExposesTypedPointers)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned");
    EXPECT_EQ(0, string->refcount);

    {
        Owned<TValue2<String>> owned_string(TValue2<String>::from_oop(string));
        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(string, owned_string.extract());
        EXPECT_STREQ(L"owned", owned_string.extract()->data);
        EXPECT_EQ(Value::from_oop(string), owned_string.raw_value());
        EXPECT_EQ(Value::from_oop(string), owned_string.value().raw_value());
        TValue2<String> borrowed_string = owned_string;
        EXPECT_EQ(string, borrowed_string.extract());
    }
    EXPECT_EQ(0, string->refcount);
}

TEST(OwnedValueState, CheckedTypedConstructionRejectsWrongTypeBeforeOwnership)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<TValue2<String>> result =
        TValue2<String>::from_value_checked(Value::None());

    EXPECT_TRUE(result.has_exception());
    EXPECT_TRUE(result.raw_value().is_exception_marker());
    EXPECT_TRUE(context.thread()->has_pending_exception());
    context.thread()->clear_pending_exception();
}

TEST(MemberValueState, ExposesValueAndReleaseRef)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member");
    EXPECT_EQ(0, string->refcount);

    Member<TValue2<String>> member_string(TValue2<String>::from_oop(string));
    EXPECT_EQ(1, string->refcount);
    EXPECT_EQ(Value::from_oop(string), member_string.raw_value());
    EXPECT_EQ(Value::from_oop(string), member_string.value().raw_value());
    TValue2<String> borrowed_string = member_string;
    EXPECT_EQ(string, borrowed_string.extract());

    member_string.release_ref();
    EXPECT_EQ(0, string->refcount);
}

TEST(OwnedValueState, SmiActsAsOwnedHandleWithoutRefcounting)
{
    Owned<TValue2<SMI>> owned_smi(TValue2<SMI>::from_smi(42));

    EXPECT_EQ(42, owned_smi.extract());
    EXPECT_EQ(Value::from_smi(42), owned_smi.raw_value());
    EXPECT_EQ(Value::from_smi(42), owned_smi.value().raw_value());

    Owned<TValue2<SMI>> copied_smi(owned_smi);
    EXPECT_EQ(Value::from_smi(42), copied_smi.raw_value());
}

TEST(OwnedValueState, ClIntActsAsOwnedIntegerHandle)
{
    Owned<TValue2<CLInt>> owned_integer(TValue2<CLInt>::from_smi(42));

    EXPECT_EQ(Value::from_smi(42), owned_integer.raw_value());

    Owned<TValue2<CLInt>> copied_integer(owned_integer);
    EXPECT_EQ(Value::from_smi(42), copied_integer.raw_value());
}
