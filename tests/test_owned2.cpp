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
