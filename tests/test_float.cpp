#include "builtin_types/float.h"
#include "builtin_types/module_object.h"
#include "object_model/function.h"
#include "object_model/shape_key.h"

#include "object_model/class_object.h"
#include "object_model/owned.h"
#include "test_helpers.h"
#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace cl
{
    TEST(FloatRuntime, BoxFloatCreatesFreshExactFloat)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());

        Owned<Value> first(box_float(context.thread(), 2.5));
        Owned<Value> second(box_float(context.thread(), 2.5));

        ASSERT_TRUE(can_convert_to<Float>(first.raw_value()));
        ASSERT_TRUE(can_convert_to<Float>(second.raw_value()));
        EXPECT_DOUBLE_EQ(2.5, first.raw_value().get_ptr<Float>()->value());
        EXPECT_DOUBLE_EQ(2.5, second.raw_value().get_ptr<Float>()->value());
        EXPECT_NE(first.raw_value(), second.raw_value());
        EXPECT_FALSE(context.thread()->has_pending_exception());
    }
    TEST(FloatRuntime, SubclassPayloadPreservesSignedZeroAndZeroInlineCapacity)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Owned<Value> result(
            context.run_file(L"class F(float): pass\nF(-0.0)\n"));
        ASSERT_FALSE(result.value().is_exception_marker());
        ASSERT_TRUE(can_convert_to<Float>(result.value()));
        Float *value = result.value().get_ptr<Float>();
        EXPECT_TRUE(std::signbit(value->value()));
        EXPECT_DOUBLE_EQ(0.0, value->value());
        EXPECT_NE(context.vm().float_class(), value->get_shape()->get_class());
        EXPECT_EQ(0u, value->get_shape()->get_inline_slot_capacity());
    }
    TEST(FloatRuntime, HashMatchesExactBinaryRationalReduction)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        CodeObject *code = context.compile_file(L"hash(value)\n");
        // Expected values were calculated independently with
        // Fraction.from_float and modular inversion using Clover's 2**58 - 1
        // hash modulus.
        struct Sample
        {
            double value;
            int64_t expected;
        };
        const Sample samples[] = {
            {0.0, 0},
            {-0.0, 0},
            {1.0, 1},
            {-1.0, -2},
            {0x1p-1, 144115188075855872},
            {-0x1p-1, -144115188075855872},
            {0x1.8p0, 144115188075855873},
            {0.1, 28823037615171176},
            {0x1p58, 1},
            {-0x1p58, -288230376151711744},
            {0x1p100, 4398046511104},
            {0x1.fffffffffffffp1023, 288221854936596479},
            {0x1p-1022, 4194304},
            {0x0.0000000000001p-1022, 268435456},
            {0x1.0000000000001p0, 65},
            {-0x1.0000000000001p58, -65},
            {0x1.fffffffffffffp57, 288230376151711712},
            {std::numeric_limits<double>::infinity(), 314159},
            {-std::numeric_limits<double>::infinity(), -314159},
        };
        for(const Sample &sample: samples)
        {
            SCOPED_TRACE(sample.value);
            ASSERT_TRUE(code->get_defining_module()
                            .extract()
                            ->set_own_property(
                                interned_string(L"value"),
                                context.thread()
                                    ->make_object_value<Float>(sample.value)
                                    .raw_value())
                            .value());
            for(int repetition = 0; repetition < 2; ++repetition)
            {
                EXPECT_EQ(Value::from_smi(sample.expected),
                          context.thread()->run_clovervm_code_object(code));
            }
        }
    }

    TEST(FloatRuntime, HashAgreesWithIntegersAndDictionaryKeys)
    {
        test::VmTestContext context;
        Value result = context.run_file(LR"(
for integer in (0, 1, -1, 17, -37, 2**57, -(2**58), 2**58, 2**100):
    assert hash(float(integer)) == hash(integer)
assert hash(1.0) == hash(True)
assert hash(0.0) == hash(False)
for integer in (0, 1, -1, 17, -(2**58)):
    value = float(integer)
    assert {integer: 42}[value] == 42
    assert {value: 43}[integer] == 43
values = {True: 7}
values[1.0] = 8
assert len(values) == 1
assert values[1] == 8
class F(float):
    pass
assert hash(F(1.5)) == hash(1.5)
assert {F(17): 42}[17.0] == 42
assert {17.0: 43}[F(17)] == 43
try:
    float.__hash__(1)
except TypeError as error:
    assert error.args[0] == "float.__hash__ expects a float receiver"
else:
    assert False
True
)");
        EXPECT_EQ(Value::True(), result);
    }

    TEST(FloatRuntime, NanHashIsStableAndSupportsDistinctDictionaryKeys)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        CodeObject *code = context.compile_file(LR"(
first_hash = hash(first)
assert first_hash == hash(first)
assert first_hash == float.__hash__(first)
values = {first: 1, second: 2}
assert len(values) == 2
assert values[first] == 1
assert values[second] == 2
True
)");
        for(const wchar_t *name: {L"first", L"second"})
        {
            ASSERT_TRUE(
                code->get_defining_module()
                    .extract()
                    ->set_own_property(
                        interned_string(name),
                        context.thread()
                            ->make_object_value<Float>(
                                std::numeric_limits<double>::quiet_NaN())
                            .raw_value())
                    .value());
        }
        EXPECT_EQ(Value::True(),
                  context.thread()->run_clovervm_code_object(code));
    }

    TEST(FloatRuntime, HashUsesTrustedCacheAndHonorsSubclassInvalidation)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        EXPECT_EQ(Value::from_smi(2),
                  context.run_file(L"hash(1.0)\nhash(2.0)\n"));
        Value function =
            context.vm().global_builtins_module().extract()->get_own_property(
                interned_string(L"hash"));
        CodeObject *hash_code =
            function.get_ptr<Function>()->code_object.extract();
        ASSERT_EQ(1u, hash_code->inline_caches.operator_caches.size());
        const OperatorInlineCache &cache =
            hash_code->inline_caches.operator_caches[0];
        ASSERT_FALSE(cache.trusted_handler.is_null());
        auto metadata = context.vm().trusted_handler_metadata(
            cache.trusted_handler.target(TrustedHandlerArity::Unary));
        ASSERT_TRUE(metadata.has_value());
        EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
        EXPECT_EQ(Value::True(), context.run_file(LR"(
class F(float):
    pass
value = F(2.0)
for i in range(2):
    assert hash(value) == 2
def replacement(self):
    return 71
F.__hash__ = replacement
for i in range(2):
    assert hash(value) == 71
assert float.__hash__(value) == 2
def failing(self):
    raise ValueError("hash callback")
F.__hash__ = failing
try:
    hash(value)
except ValueError as error:
    assert error.args[0] == "hash callback"
else:
    assert False
del F.__hash__
assert hash(value) == 2
True
)"));
    }
    TEST(FloatRuntime, BoolMethodsAndTrustedHandlersCoverSpecialValues)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Value method = context.vm().float_class()->get_own_property(
            interned_string(L"__bool__"));
        ASSERT_TRUE(can_convert_to<Function>(method));
        auto resolver = method.get_ptr<Function>()
                            ->code_object.extract()
                            ->trusted_handler_resolver;
        ASSERT_NE(nullptr, resolver);
        CodeObject *code = context.compile_file(L"float.__bool__(value)\n");
        const double values[] = {0.0,
                                 -0.0,
                                 1.5,
                                 -1.5,
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()};
        for(double number: values)
        {
            Owned<Value> value(
                context.thread()->make_object_value<Float>(number).raw_value());
            Value expected = number != 0.0 ? Value::True() : Value::False();
            ASSERT_TRUE(
                code->get_defining_module()
                    .extract()
                    ->set_own_property(interned_string(L"value"), value.value())
                    .value());
            EXPECT_EQ(expected,
                      context.thread()->run_clovervm_code_object(code));
            auto resolution = resolver(
                &context.vm(), ShapeKey::from_value(value.value()),
                ShapeKey::from_value(Value::not_present()),
                TrustedHandlerOperandOrder::Normal, TrustedHandlerArity::Unary);
            ASSERT_TRUE(resolution.has_trusted_handler());
            EXPECT_EQ(expected,
                      resolution.unary(context.thread(), value.value()));
            auto metadata = context.vm().trusted_handler_metadata(
                erase_trusted_handler_target(resolution.unary));
            ASSERT_TRUE(metadata.has_value());
            EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
        }
        Owned<Value> subclass(
            context.run_file(L"class F(float): pass\nF(0.0)\n"));
        EXPECT_FALSE(resolver(&context.vm(),
                              ShapeKey::from_value(subclass.value()),
                              ShapeKey::from_value(Value::not_present()),
                              TrustedHandlerOperandOrder::Normal,
                              TrustedHandlerArity::Unary)
                         .has_trusted_handler());
    }
}  // namespace cl
