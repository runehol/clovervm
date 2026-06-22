#include "builtin_types/bigint.h"
#include "builtin_types/hash.h"
#include "builtin_types/str.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <gtest/gtest.h>

using namespace cl;

namespace
{
    Value make_heap_bigint(test::VmTestContext &context, int64_t value)
    {
        ThreadState *thread = context.thread();
        SmiViewStorage storage;
        ConstBigIntView view = smi_bigint_view(value, &storage);
        BigInt *bigint = make_uninitialized_bigint_for_digits(
            thread, view.n_digits, view.signum);
        MutableBigIntView dest = bigint->mutable_view_for_initialization();
        for(size_t idx = 0; idx < view.n_digits; ++idx)
        {
            dest.digits[idx] = view.digits[idx];
        }
        return Value::from_oop(bigint);
    }

    void expect_pending_exception(ThreadState *thread,
                                  const wchar_t *class_name,
                                  const wchar_t *message)
    {
        ASSERT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        TValue<Exception> exception = thread->pending_exception_object();
        EXPECT_STREQ(class_name, exception.extract()
                                     ->get_shape()
                                     ->get_class()
                                     ->get_name()
                                     .extract()
                                     ->data);
        EXPECT_STREQ(message, exception.extract()->message.extract()->data);
    }
}  // namespace

TEST(Hash, CanonicalizeHashPreservesBoolAndSmiExceptMinusOne)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    EXPECT_EQ(0, canonicalize_hash_result(Value::False()).value().extract());
    EXPECT_EQ(1, canonicalize_hash_result(Value::True()).value().extract());
    EXPECT_EQ(42,
              canonicalize_hash_result(Value::from_smi(42)).value().extract());
    EXPECT_EQ(-2,
              canonicalize_hash_result(Value::from_smi(-1)).value().extract());
    EXPECT_EQ(value_smi_min,
              canonicalize_hash_result(Value::from_smi(value_smi_min))
                  .value()
                  .extract());
    EXPECT_EQ(value_smi_max,
              canonicalize_hash_result(Value::from_smi(value_smi_max))
                  .value()
                  .extract());
}

TEST(Hash, CanonicalizeHashPreservesFittingHeapBigInt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value bigint = make_heap_bigint(context, 123);

    EXPECT_EQ(123, canonicalize_hash_result(bigint).value().extract());
}

TEST(Hash, CanonicalizeHashReducesOutOfSmiBigInt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<Value> positive =
        bigint_from_int64(context.thread(), value_smi_max + 1);
    ASSERT_TRUE(positive.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(positive.value()));
    EXPECT_EQ(1, canonicalize_hash_result(positive.value()).value().extract());

    Expected<Value> negative =
        bigint_from_int64(context.thread(), -(2 * clover_hash_modulus + 1));
    ASSERT_TRUE(negative.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(negative.value()));
    EXPECT_EQ(-2, canonicalize_hash_result(negative.value()).value().extract());
}

TEST(Hash, CanonicalizeHashRejectsNonInteger)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Value value =
        context.thread()->make_internal_value<String>(L"not-int").raw_value();

    Expected<TValue<SMI>> result = canonicalize_hash_result(value);

    EXPECT_TRUE(result.has_exception());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Hash, StringHashNormalizedFitsSmi)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<String> value = context.thread()->make_internal_value<String>(
        L"this string is long enough to overflow raw smi hash storage");

    uint64_t raw = string_hash(value);
    TValue<SMI> normalized = string_hash_normalized(value);

    EXPECT_GT(raw, uint64_t(value_smi_max));
    EXPECT_GE(normalized.extract(), 0);
    EXPECT_LT(normalized.extract(), clover_hash_modulus);
}
