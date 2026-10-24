#include "builtin_types/bigint.h"
#include "builtin_types/hash.h"
#include "builtin_types/str.h"
#include "object_model/attr.h"
#include "object_model/function.h"
#include "object_model/shape_key.h"
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
        EXPECT_STREQ(
            message,
            cl::test::native_exception_message(exception).extract()->data);
    }
}  // namespace

TEST(Hash, NoneHashResolverGuardsReceiverAndArity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Value method = load_attr(Value::from_oop(context.vm().none_type_class()),
                             interned_string(L"__hash__"));
    ASSERT_TRUE(can_convert_to<Function>(method));
    CodeObject *code = method.get_ptr<Function>()->code_object.extract();
    ASSERT_NE(nullptr, code->trusted_handler_resolver);

    ShapeKey none_key = ShapeKey::from_value(Value::None());
    ShapeKey unused_key = ShapeKey::from_value(Value::not_present());
    TrustedResolution resolution = code->trusted_handler_resolver(
        &context.vm(), none_key, unused_key, TrustedHandlerOperandOrder::Normal,
        TrustedHandlerArity::Unary);
    ASSERT_TRUE(resolution.has_trusted_handler());
    ASSERT_EQ(TrustedHandlerArity::Unary, resolution.arity);
    EXPECT_EQ(Value::from_smi(4238894112),
              resolution.unary(context.thread(), Value::None()));
    auto metadata = context.vm().trusted_handler_metadata(
        erase_trusted_handler_target(resolution.unary));
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(TrustedHandlerArity::Unary, metadata->arity);
    EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
    EXPECT_EQ(TrustedHandlerSemantics::Generic, metadata->semantics);

    EXPECT_FALSE(code->trusted_handler_resolver(
                         &context.vm(),
                         ShapeKey::from_value(Value::from_smi(0)), unused_key,
                         TrustedHandlerOperandOrder::Normal,
                         TrustedHandlerArity::Unary)
                     .has_trusted_handler());
    EXPECT_FALSE(
        code->trusted_handler_resolver(&context.vm(), none_key, unused_key,
                                       TrustedHandlerOperandOrder::Normal,
                                       TrustedHandlerArity::Binary)
            .has_trusted_handler());
}

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
        int_value_from_int64(context.thread(), value_smi_max + 1);
    ASSERT_TRUE(positive.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(positive.value()));
    EXPECT_EQ(1, canonicalize_hash_result(positive.value()).value().extract());

    Expected<Value> negative =
        int_value_from_int64(context.thread(), -(2 * clover_hash_modulus + 1));
    ASSERT_TRUE(negative.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(negative.value()));
    EXPECT_EQ(-2, canonicalize_hash_result(negative.value()).value().extract());
}

TEST(Hash, CanonicalizeHashReducesLargeBigIntWithoutOverflowingIntermediate)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value ten_to_100 = context.run_file(L"10**100\n");
    ASSERT_TRUE(can_convert_to<BigInt>(ten_to_100));
    EXPECT_EQ(69889855055785222,
              canonicalize_hash_result(ten_to_100).value().extract());

    Value ten_to_101 = context.run_file(L"10**101\n");
    ASSERT_TRUE(can_convert_to<BigInt>(ten_to_101));
    EXPECT_EQ(122437798254428734,
              canonicalize_hash_result(ten_to_101).value().extract());
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
