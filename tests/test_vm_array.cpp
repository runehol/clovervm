#include "test_helpers.h"
#include "thread_state.h"
#include "vm_array.h"
#include <gtest/gtest.h>

using namespace cl;

namespace
{
    struct ValuePair
    {
        Value first;
        TValue<SMI> second;
    };

    struct HeapPtrArrayItem : public HeapObject
    {
        HeapPtrArrayItem() : HeapObject(compact_layout()) {}

        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(HeapPtrArrayItem);
    };

    static_assert(std::is_standard_layout_v<ValuePair>);
    static_assert(std::is_trivially_destructible_v<ValuePair>);
    static_assert(sizeof(ValuePair) == sizeof(Value) * 2);

    struct ArrayOwner : public HeapObject
    {
        ArrayOwner() : HeapObject(compact_layout()) {}

        RawArray<int32_t> raw_values;
        ValueArray<TValue<String>> typed_values;
        ValueArray<ValuePair> pair_values;
        HeapPtrArray<HeapPtrArrayItem> heap_ptrs;

        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(
            ArrayOwner, raw_values,
            RawArray<int32_t>::embedded_value_count +
                ValueArray<TValue<String>>::embedded_value_count +
                ValueArray<ValuePair>::embedded_value_count +
                HeapPtrArray<HeapPtrArrayItem>::embedded_value_count);
    };
}  // namespace

TEST(RawArray, GrowsAndPreservesValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    ArrayOwner *owner = context.thread()->make_internal_raw<ArrayOwner>();

    RawArray<int32_t> &values = owner->raw_values;
    values.resize(2, -1);
    values.push_back(7);
    values.emplace_back(9);

    ASSERT_EQ(4u, values.size());
    EXPECT_EQ(4, values.len().extract());
    EXPECT_GE(values.capacity(), values.size());
    EXPECT_EQ(-1, values[0]);
    EXPECT_EQ(-1, values[1]);
    EXPECT_EQ(7, values[2]);
    EXPECT_EQ(9, values[3]);

    values.resize(3);
    ASSERT_EQ(3u, values.size());
    EXPECT_EQ(7, values.back());
}

TEST(ValueArray, SupportsTypedValueElementsAcrossGrowth)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    ArrayOwner *owner = context.thread()->make_internal_raw<ArrayOwner>();

    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"answer"));

    ValueArray<TValue<String>> &values = owner->typed_values;
    values.emplace_back(name);
    values.reserve(8);

    ASSERT_EQ(1u, values.size());
    EXPECT_GE(values.capacity(), 8u);
    EXPECT_STREQ(L"answer", values[0].extract()->data);
}

TEST(ValueArray, SupportsFlatValueStructElements)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    ArrayOwner *owner = context.thread()->make_internal_raw<ArrayOwner>();

    ValueArray<ValuePair> &values = owner->pair_values;
    values.push_back(
        ValuePair{Value::from_smi(11),
                  TValue<SMI>::from_value_checked(Value::from_smi(23))});
    values.reserve(4);

    ASSERT_EQ(1u, values.size());
    EXPECT_EQ(Value::from_smi(11), values[0].first);
    EXPECT_EQ(23, values[0].second.extract());
}

TEST(HeapPtrArray, SupportsTypedHeapPointersAcrossGrowth)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    ArrayOwner *owner = context.thread()->make_internal_raw<ArrayOwner>();

    HeapPtrArrayItem *first =
        context.thread()->make_internal_raw<HeapPtrArrayItem>();
    HeapPtrArrayItem *second =
        context.thread()->make_internal_raw<HeapPtrArrayItem>();

    HeapPtrArray<HeapPtrArrayItem> &values = owner->heap_ptrs;
    values.push_back(first);
    values.reserve(8);
    values.push_back(second);

    ASSERT_EQ(2u, values.size());
    EXPECT_GE(values.capacity(), 8u);
    EXPECT_EQ(first, values[0]);
    EXPECT_EQ(second, values[1]);
    EXPECT_EQ(1, first->refcount);
    EXPECT_EQ(1, second->refcount);

    values.clear();
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(0, first->refcount);
    EXPECT_EQ(0, second->refcount);
}
