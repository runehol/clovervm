#include "scope.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "typed_value.h"
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

TEST(TValue, StringAllowsCheckedConstructionFromNonInternedString)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_refcounted_raw<String>(L"hello");

    TValue<String> typed_string = TValue<String>::from_oop(string);

    EXPECT_EQ(string, typed_string.get());
    EXPECT_STREQ(L"hello", typed_string.get()->data);
}

TEST(TValue, ScopeUsesConcreteKlassTrait)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *scope = context.thread()->make_refcounted_raw<Scope>(Value::None());

    TValue<Scope> typed_scope = TValue<Scope>::from_oop(scope);

    EXPECT_EQ(scope, typed_scope.get());
    EXPECT_TRUE(typed_scope.get()->empty());
}

TEST(TValue, UnsafeUncheckedRoundTripsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_refcounted_raw<String>(L"unsafe");
    Value raw = Value::from_oop(string);

    TValue<String> typed_string = TValue<String>::unsafe_unchecked(raw);

    EXPECT_EQ(raw, typed_string.as_value());
    EXPECT_EQ(string, typed_string.get());
}

TEST(TValue, CheckedConstructionThrowsOnWrongType)
{
    EXPECT_THROW((void)TValue<String>(Value::True()), std::runtime_error);
}

TEST(TValue, SmiUsesTraitDefinedGetter)
{
    TValue<SMI> smi(Value::from_smi(42));

    EXPECT_EQ(42, smi.get());
}

TEST(TValue, ClIntAcceptsCurrentIntegerRepresentation)
{
    TValue<CLInt> integer(Value::from_smi(42));

    EXPECT_EQ(Value::from_smi(42), integer.as_value());
}

TEST(OwnedTValue, RetainsAndExposesTypedPointers)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_refcounted_raw<String>(L"owned");
    EXPECT_EQ(0, string->refcount);

    OwnedTValue<String> owned_string(Value::from_oop(string));
    EXPECT_EQ(1, string->refcount);
    EXPECT_EQ(string, owned_string.get());
    EXPECT_STREQ(L"owned", owned_string.get()->data);

    Value released = owned_string.release();
    EXPECT_EQ(Value::from_oop(string), released);
    EXPECT_EQ(1, string->refcount);
}

TEST(OwnedTValue, CheckedConstructionThrowsOnWrongType)
{
    EXPECT_THROW((void)OwnedTValue<Scope>(Value::None()), std::runtime_error);
}

TEST(OwnedTValue, SmiActsAsOwnedHandleWithoutRefcounting)
{
    OwnedTValue<SMI> owned_smi(Value::from_smi(42));

    EXPECT_EQ(42, owned_smi.get());

    Value released = owned_smi.release();
    EXPECT_EQ(Value::from_smi(42), released);
    EXPECT_EQ(Value::None(), Value(owned_smi));
}

TEST(OwnedTValue, ClIntActsAsOwnedIntegerHandle)
{
    OwnedTValue<CLInt> owned_integer(Value::from_smi(42));

    EXPECT_EQ(Value::from_smi(42), owned_integer.as_value());

    Value released = owned_integer.release();
    EXPECT_EQ(Value::from_smi(42), released);
    EXPECT_EQ(Value::None(), Value(owned_integer));
}
