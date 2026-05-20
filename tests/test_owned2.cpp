#include "owned2.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "value_state.h"
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

using namespace cl;

namespace
{
    Value raw_value_from_typed_string(TValue2<String> value)
    {
        return value.raw_value();
    }

    Value raw_value_from_value(Value value) { return value.raw_value(); }

    template <typename T, typename = void>
    struct HasReleaseRef : std::false_type
    {
    };

    template <typename T>
    struct HasReleaseRef<
        T, std::void_t<decltype(std::declval<T &>().release_ref())>>
        : std::true_type
    {
    };
}  // namespace

TEST(HandleRefcountTraits, ClassifiesFullyInlineSemanticTypes)
{
    static_assert(HandleRefcountTraits<TValue2<SMI>>::is_fully_inline);
    static_assert(HandleRefcountTraits<TValue2<Bool>>::is_fully_inline);
    static_assert(HandleRefcountTraits<TValue2<None>>::is_fully_inline);
    static_assert(
        HandleRefcountTraits<Expected<TValue2<None>>>::is_fully_inline);
    static_assert(
        HandleRefcountTraits<Optional<TValue2<Bool>>>::is_fully_inline);

    static_assert(!HandleRefcountTraits<Value>::is_fully_inline);
    static_assert(!HandleRefcountTraits<TValue2<String>>::is_fully_inline);
    static_assert(
        !HandleRefcountTraits<Optional<TValue2<String>>>::is_fully_inline);
}

TEST(Owned2, StoresTypedWrapperAndRetainsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    static_assert(
        std::is_same_v<Owned2<TValue2<String>>::semantic_type, String>);
    static_assert(sizeof(Owned2<TValue2<String>>) == sizeof(TValue2<String>));
    static_assert(!HasReleaseRef<Owned2<TValue2<String>>>::value);

    EXPECT_EQ(0, string->refcount);
    {
        Owned2<TValue2<String>> owned(typed_string);

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(typed_string.raw_value(), owned.raw_value());
        EXPECT_EQ(string, owned.value().extract());
        EXPECT_EQ(string, (*owned).extract());
    }
    EXPECT_EQ(0, string->refcount);
}

TEST(Owned2, StoresRawValueAndRetainsIt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    Value value = Value::from_oop(string);

    static_assert(std::is_same_v<Owned2<Value>::semantic_type, Value>);
    static_assert(sizeof(Owned2<Value>) == sizeof(Value));
    static_assert(!std::is_default_constructible_v<Owned2<Value>>);
    static_assert(!HasReleaseRef<Owned2<Value>>::value);

    EXPECT_EQ(0, string->refcount);
    {
        Owned2<Value> owned(value);

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(value, owned.raw_value());
        EXPECT_EQ(value, owned.value());
        EXPECT_EQ(value, raw_value_from_value(owned));
    }
    EXPECT_EQ(0, string->refcount);
}

TEST(Owned2, ConvertsToWrappedType)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);
    Owned2<TValue2<String>> owned(typed_string);

    EXPECT_EQ(typed_string.raw_value(), raw_value_from_typed_string(owned));
}

TEST(Owned2, CopyRetainsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    EXPECT_EQ(0, string->refcount);
    {
        Owned2<TValue2<String>> first(typed_string);
        EXPECT_EQ(1, string->refcount);
        {
            Owned2<TValue2<String>> second(first);
            EXPECT_EQ(2, string->refcount);
            EXPECT_EQ(string, second.value().extract());
        }
        EXPECT_EQ(1, string->refcount);
    }
    EXPECT_EQ(0, string->refcount);
}

TEST(Owned2, ComparesByRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *first = context.thread()->make_internal_raw<String>(L"first");
    String *second = context.thread()->make_internal_raw<String>(L"second");

    Owned2<TValue2<String>> owned_first(TValue2<String>::from_oop(first));
    Owned2<TValue2<String>> owned_first_again(TValue2<String>::from_oop(first));
    Owned2<TValue2<String>> owned_second(TValue2<String>::from_oop(second));

    EXPECT_EQ(owned_first, owned_first_again);
    EXPECT_NE(owned_first, owned_second);
}

TEST(Member2, StoresTypedWrapperAndRetainsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    static_assert(
        std::is_same_v<Member2<TValue2<String>>::semantic_type, String>);
    static_assert(sizeof(Member2<TValue2<String>>) == sizeof(TValue2<String>));

    EXPECT_EQ(0, string->refcount);
    {
        Member2<TValue2<String>> member(typed_string);

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(typed_string.raw_value(), member.raw_value());
        EXPECT_EQ(string, member.value().extract());
        EXPECT_EQ(string, (*member).extract());
    }
    EXPECT_EQ(1, string->refcount);

    decref(Value::from_oop(string));
    EXPECT_EQ(0, string->refcount);
}

TEST(Member2, StoresRawValueAndRetainsIt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    Value value = Value::from_oop(string);

    static_assert(std::is_same_v<Member2<Value>::semantic_type, Value>);
    static_assert(sizeof(Member2<Value>) == sizeof(Value));
    static_assert(!std::is_default_constructible_v<Member2<Value>>);
    static_assert(HasReleaseRef<Member2<Value>>::value);

    EXPECT_EQ(0, string->refcount);
    Member2<Value> member(value);

    EXPECT_EQ(1, string->refcount);
    EXPECT_EQ(value, member.raw_value());
    EXPECT_EQ(value, member.value());
    EXPECT_EQ(value, raw_value_from_value(member));

    member.release_ref();
    EXPECT_EQ(0, string->refcount);
}

TEST(Member2, ConvertsToWrappedType)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);
    Member2<TValue2<String>> member(typed_string);

    EXPECT_EQ(typed_string.raw_value(), raw_value_from_typed_string(member));
    member.release_ref();
}

TEST(Member2, ComparesByRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *first = context.thread()->make_internal_raw<String>(L"first");
    String *second = context.thread()->make_internal_raw<String>(L"second");

    Member2<TValue2<String>> member_first(TValue2<String>::from_oop(first));
    Member2<TValue2<String>> member_first_again(
        TValue2<String>::from_oop(first));
    Member2<TValue2<String>> member_second(TValue2<String>::from_oop(second));
    Owned2<TValue2<String>> owned_first(TValue2<String>::from_oop(first));

    EXPECT_EQ(member_first, member_first_again);
    EXPECT_NE(member_first, member_second);
    EXPECT_EQ(member_first, owned_first);
    EXPECT_EQ(owned_first, member_first);
    EXPECT_NE(member_second, owned_first);
    EXPECT_NE(owned_first, member_second);
}

TEST(Member2, ReleaseRefReleasesRawValueForDealloc)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);
    Member2<TValue2<String>> member(typed_string);

    EXPECT_EQ(1, string->refcount);

    member.release_ref();

    EXPECT_EQ(0, string->refcount);
}

TEST(Member2, AssignmentRetainsNewValueAndReleasesOldValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *first = context.thread()->make_internal_raw<String>(L"first");
    String *second = context.thread()->make_internal_raw<String>(L"second");
    using MaybeString = Optional<TValue2<String>>;

    Member2<MaybeString> member(
        MaybeString::some(TValue2<String>::from_oop(first)));
    EXPECT_EQ(1, first->refcount);
    EXPECT_EQ(0, second->refcount);

    member = MaybeString::some(TValue2<String>::from_oop(second));
    EXPECT_EQ(0, first->refcount);
    EXPECT_EQ(1, second->refcount);
    EXPECT_EQ(second, member.value().value().extract());

    member = MaybeString::none();
    EXPECT_EQ(0, second->refcount);
    EXPECT_TRUE(member.raw_value().is_none());
}
