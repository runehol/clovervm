#include "builtin_types/bigint.h"
#include "builtin_types/dict.h"
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
    ASSERT_TRUE(has_native_layout<Function>(method));
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
    ASSERT_TRUE(has_native_layout<BigInt>(positive.value()));
    EXPECT_EQ(1, canonicalize_hash_result(positive.value()).value().extract());

    Expected<Value> negative =
        int_value_from_int64(context.thread(), -(2 * clover_hash_modulus + 1));
    ASSERT_TRUE(negative.has_value());
    ASSERT_TRUE(has_native_layout<BigInt>(negative.value()));
    EXPECT_EQ(-2, canonicalize_hash_result(negative.value()).value().extract());
}

TEST(Hash, CanonicalizeHashReducesLargeBigIntWithoutOverflowingIntermediate)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value ten_to_100 = context.run_file(L"10**100\n");
    ASSERT_TRUE(has_native_layout<BigInt>(ten_to_100));
    EXPECT_EQ(69889855055785222,
              canonicalize_hash_result(ten_to_100).value().extract());

    Value ten_to_101 = context.run_file(L"10**101\n");
    ASSERT_TRUE(has_native_layout<BigInt>(ten_to_101));
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

TEST(Hash, TupleHashResolverGuardsReceiverOrderAndArity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Value method = load_attr(Value::from_oop(context.vm().tuple_class()),
                             interned_string(L"__hash__"));
    ASSERT_TRUE(has_native_layout<Function>(method));
    CodeObject *code = method.get_ptr<Function>()->code_object.extract();
    ASSERT_NE(nullptr, code->trusted_handler_resolver);
    Owned<TValue<Tuple>> tuple(context.thread()->make_object_value<Tuple>(0));
    ShapeKey key = ShapeKey::from_value(tuple.raw_value());
    ShapeKey unused = ShapeKey::from_value(Value::not_present());
    TrustedResolution resolution = code->trusted_handler_resolver(
        &context.vm(), key, unused, TrustedHandlerOperandOrder::Normal,
        TrustedHandlerArity::Unary);
    ASSERT_TRUE(resolution.has_trusted_handler());
    EXPECT_EQ(TrustedHandlerArity::Unary, resolution.arity);
    auto metadata = context.vm().trusted_handler_metadata(
        erase_trusted_handler_target(resolution.unary));
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(TrustedHandlerArity::Unary, metadata->arity);
    EXPECT_EQ(
        TrustedHandlerEffects::Allocate | TrustedHandlerEffects::Safepoint |
            TrustedHandlerEffects::Raise | TrustedHandlerEffects::CallPython,
        metadata->effects);
    EXPECT_EQ(TrustedHandlerSemantics::Generic, metadata->semantics);
    Value hash = resolution.unary(context.thread(), tuple.raw_value());
    ASSERT_TRUE(hash.is_smi());
    EXPECT_GE(hash.get_smi(), 0);
    EXPECT_LT(hash.get_smi(), clover_hash_modulus);
    auto protocol_hash = context.thread()->hash_value(tuple.raw_value());
    ASSERT_TRUE(protocol_hash.has_value());
    EXPECT_EQ(hash, protocol_hash.value().raw_value());

    Owned<Value> subclass(
        context.run_file(L"class T(tuple):\n    pass\nT((1,))\n"));
    ASSERT_TRUE(has_native_layout<Tuple>(subclass.value()));
    for(ShapeKey rejected: {ShapeKey::from_value(Value::from_smi(0)),
                            ShapeKey::from_value(Value::None()),
                            ShapeKey::from_value(subclass.value())})
    {
        EXPECT_FALSE(
            code->trusted_handler_resolver(&context.vm(), rejected, unused,
                                           TrustedHandlerOperandOrder::Normal,
                                           TrustedHandlerArity::Unary)
                .has_trusted_handler());
    }
    EXPECT_FALSE(
        code->trusted_handler_resolver(&context.vm(), key, unused,
                                       TrustedHandlerOperandOrder::Reflected,
                                       TrustedHandlerArity::Unary)
            .has_trusted_handler());
    EXPECT_FALSE(
        code->trusted_handler_resolver(&context.vm(), key, unused,
                                       TrustedHandlerOperandOrder::Normal,
                                       TrustedHandlerArity::Binary)
            .has_trusted_handler());
}

TEST(Hash, TupleHashRecomputesElementsAndPropagatesDirectReceiverError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context
                     .run_file(LR"PY(
class Element:
    def __hash__(self):
        return self.result
item = Element()
item.result = 3
value = (item,)
def hash_one(x):
    return hash(x)
for i in range(4):
    assert hash_one(value) == hash_one((3,))
item.result = 5
for i in range(4):
    assert hash_one(value) == hash_one((5,))
def replacement(self):
    return 7
Element.__hash__ = replacement
for i in range(4):
    assert hash_one(value) == hash_one((7,))
)PY")
                     .is_exception_marker());
    EXPECT_TRUE(context.run_file(L"tuple.__hash__(1)\n").is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"tuple.__hash__ expects a tuple receiver");
}

TEST(Hash, TupleKeysUseNativeDictionaryHashProtocolDuringReclamation)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    Owned<Value> keys(context.run_file(LR"PY(
class Element:
    def __init__(self, value):
        self.value = value
    def __hash__(self):
        garbage = []
        for i in range(20):
            garbage.append([i])
        return self.value
    def __eq__(self, other):
        return self.value == other.value
((Element(5),), (Element(5),), ([],))
)PY"));
    ASSERT_TRUE(has_native_layout<Tuple>(keys.value()));
    Owned<TValue<Dict>> dict(thread->make_object_value<Dict>());
    Value first = keys.value().get_ptr<Tuple>()->item_unchecked(0);
    Value equal = keys.value().get_ptr<Tuple>()->item_unchecked(1);
    Value invalid = keys.value().get_ptr<Tuple>()->item_unchecked(2);
    ASSERT_FALSE(dict.extract()
                     ->set_item(thread, first, Value::from_smi(31))
                     .has_exception());
    auto found = dict.extract()->get_item(thread, equal);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(Value::from_smi(31), found.value());
    auto contains = dict.extract()->contains(thread, equal);
    ASSERT_TRUE(contains.has_value());
    EXPECT_TRUE(contains.value());
    ASSERT_FALSE(dict.extract()->del_item(thread, equal).has_exception());
    EXPECT_TRUE(dict.extract()
                    ->set_item(thread, invalid, Value::None())
                    .has_exception());
    expect_pending_exception(thread, L"TypeError", L"object is unhashable");
}
