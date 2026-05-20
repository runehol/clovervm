#include "owned.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "typed_value.h"
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

TEST(Owned, StoresTypedWrapperAndRetainsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    static_assert(
        std::is_same_v<Owned<TValue2<String>>::semantic_type, String>);
    static_assert(sizeof(Owned<TValue2<String>>) == sizeof(TValue2<String>));
    static_assert(!HasReleaseRef<Owned<TValue2<String>>>::value);

    EXPECT_EQ(0, string->refcount);
    {
        Owned<TValue2<String>> owned(typed_string);

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(typed_string.raw_value(), owned.raw_value());
        EXPECT_EQ(string, owned.value().extract());
        EXPECT_EQ(string, (*owned).extract());
    }
    EXPECT_EQ(0, string->refcount);
}

TEST(Owned, StoresRawValueAndRetainsIt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    Value value = Value::from_oop(string);

    static_assert(std::is_same_v<Owned<Value>::semantic_type, Value>);
    static_assert(sizeof(Owned<Value>) == sizeof(Value));
    static_assert(!std::is_default_constructible_v<Owned<Value>>);
    static_assert(!std::is_default_constructible_v<Owned<TValue2<String>>>);
    static_assert(
        std::is_default_constructible_v<Owned<Optional<TValue2<String>>>>);
    static_assert(!HasReleaseRef<Owned<Value>>::value);

    EXPECT_EQ(0, string->refcount);
    {
        Owned<Value> owned(value);

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(value, owned.raw_value());
        EXPECT_EQ(value, owned.value());
        EXPECT_EQ(value, raw_value_from_value(owned));
    }
    EXPECT_EQ(0, string->refcount);
}

TEST(Owned, DefaultConstructsWhenWrappedTypeHasDefault)
{
    Owned<Optional<TValue2<String>>> owned;

    EXPECT_FALSE(owned.value().has_value());
    EXPECT_EQ(Value::None(), owned.raw_value());
}

TEST(Owned, ConvertsToWrappedType)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);
    Owned<TValue2<String>> owned(typed_string);

    EXPECT_EQ(typed_string.raw_value(), raw_value_from_typed_string(owned));
}

TEST(Owned, CopyRetainsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"owned2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    EXPECT_EQ(0, string->refcount);
    {
        Owned<TValue2<String>> first(typed_string);
        EXPECT_EQ(1, string->refcount);
        {
            Owned<TValue2<String>> second(first);
            EXPECT_EQ(2, string->refcount);
            EXPECT_EQ(string, second.value().extract());
        }
        EXPECT_EQ(1, string->refcount);
    }
    EXPECT_EQ(0, string->refcount);
}

TEST(Owned, ComparesByRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *first = context.thread()->make_internal_raw<String>(L"first");
    String *second = context.thread()->make_internal_raw<String>(L"second");

    Owned<TValue2<String>> owned_first(TValue2<String>::from_oop(first));
    Owned<TValue2<String>> owned_first_again(TValue2<String>::from_oop(first));
    Owned<TValue2<String>> owned_second(TValue2<String>::from_oop(second));

    EXPECT_EQ(owned_first, owned_first_again);
    EXPECT_NE(owned_first, owned_second);
}

TEST(Member, StoresTypedWrapperAndRetainsRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);

    static_assert(
        std::is_same_v<Member<TValue2<String>>::semantic_type, String>);
    static_assert(sizeof(Member<TValue2<String>>) == sizeof(TValue2<String>));

    EXPECT_EQ(0, string->refcount);
    {
        Member<TValue2<String>> member(typed_string);

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(typed_string.raw_value(), member.raw_value());
        EXPECT_EQ(string, member.value().extract());
        EXPECT_EQ(string, (*member).extract());
    }
    EXPECT_EQ(1, string->refcount);

    decref(Value::from_oop(string));
    EXPECT_EQ(0, string->refcount);
}

TEST(Member, StoresRawValueAndRetainsIt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    Value value = Value::from_oop(string);

    static_assert(std::is_same_v<Member<Value>::semantic_type, Value>);
    static_assert(sizeof(Member<Value>) == sizeof(Value));
    static_assert(!std::is_default_constructible_v<Member<Value>>);
    static_assert(!std::is_default_constructible_v<Member<TValue2<String>>>);
    static_assert(
        std::is_default_constructible_v<Member<Optional<TValue2<String>>>>);
    static_assert(HasReleaseRef<Member<Value>>::value);

    EXPECT_EQ(0, string->refcount);
    Member<Value> member(value);

    EXPECT_EQ(1, string->refcount);
    EXPECT_EQ(value, member.raw_value());
    EXPECT_EQ(value, member.value());
    EXPECT_EQ(value, raw_value_from_value(member));

    member.release_ref();
    EXPECT_EQ(0, string->refcount);
}

TEST(Member, DefaultConstructsWhenWrappedTypeHasDefault)
{
    Member<Optional<TValue2<String>>> member;

    EXPECT_FALSE(member.value().has_value());
    EXPECT_EQ(Value::None(), member.raw_value());

    member.release_ref();
}

TEST(Member, ConvertsToWrappedType)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);
    Member<TValue2<String>> member(typed_string);

    EXPECT_EQ(typed_string.raw_value(), raw_value_from_typed_string(member));
    member.release_ref();
}

TEST(Member, ComparesByRawValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *first = context.thread()->make_internal_raw<String>(L"first");
    String *second = context.thread()->make_internal_raw<String>(L"second");

    Member<TValue2<String>> member_first(TValue2<String>::from_oop(first));
    Member<TValue2<String>> member_first_again(
        TValue2<String>::from_oop(first));
    Member<TValue2<String>> member_second(TValue2<String>::from_oop(second));
    Owned<TValue2<String>> owned_first(TValue2<String>::from_oop(first));

    EXPECT_EQ(member_first, member_first_again);
    EXPECT_NE(member_first, member_second);
    EXPECT_EQ(member_first, owned_first);
    EXPECT_EQ(owned_first, member_first);
    EXPECT_NE(member_second, owned_first);
    EXPECT_NE(owned_first, member_second);
}

TEST(Member, ReleaseRefReleasesRawValueForDealloc)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"member2");
    TValue2<String> typed_string = TValue2<String>::from_oop(string);
    Member<TValue2<String>> member(typed_string);

    EXPECT_EQ(1, string->refcount);

    member.release_ref();

    EXPECT_EQ(0, string->refcount);
}

TEST(Member, AssignmentRetainsNewValueAndReleasesOldValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *first = context.thread()->make_internal_raw<String>(L"first");
    String *second = context.thread()->make_internal_raw<String>(L"second");
    using MaybeString = Optional<TValue2<String>>;

    Member<MaybeString> member(
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
