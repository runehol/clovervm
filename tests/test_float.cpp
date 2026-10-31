#include "builtin_types/float.h"
#include "builtin_types/module_object.h"
#include "object_model/function.h"
#include "object_model/shape_key.h"

#include "object_model/class_object.h"
#include "object_model/owned.h"
#include "test_helpers.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace cl
{
    TEST(FloatRuntime, MixedHeapIntegerArithmeticTrustedReplay)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        struct Operation
        {
            const wchar_t *normal;
            const wchar_t *reflected;
            const wchar_t *source;
            TrustedHandlerSemantics semantics;
            bool raises_without_heap_integer;
            double forward;
            double reverse;
        };
        const Operation operations[] = {
            {L"__add__", L"__radd__",
             L"def calculate(a, b): return a + b\ncalculate\n",
             TrustedHandlerSemantics::Add, false, 0x1p60, 0x1p60},
            {L"__sub__", L"__rsub__",
             L"def calculate(a, b): return a - b\ncalculate\n",
             TrustedHandlerSemantics::Sub, false, -0x1p60, 0x1p60},
            {L"__mul__", L"__rmul__",
             L"def calculate(a, b): return a * b\ncalculate\n",
             TrustedHandlerSemantics::Mul, false, 0x1p60, 0x1p60},
            {L"__truediv__", L"__rtruediv__",
             L"def calculate(a, b): return a / b\ncalculate\n",
             TrustedHandlerSemantics::TrueDiv, true, 0x1p-60, 0x1p60},
            {L"__floordiv__", L"__rfloordiv__",
             L"def calculate(a, b): return a // b\ncalculate\n",
             TrustedHandlerSemantics::FloorDiv, true, 0.0, 0x1p60},
            {L"__mod__", L"__rmod__",
             L"def calculate(a, b): return a % b\ncalculate\n",
             TrustedHandlerSemantics::Mod, true, 1.0, 0.0},
        };
        Owned<Value> integer(context.run_file(L"2**60\n"));
        Owned<Value> huge(context.run_file(L"10**400\n"));
        Owned<Value> number(box_float(context.thread(), 1.0));
        Owned<Value> subclass(
            context.run_file(L"class I(int): pass\nI(2**60)\n"));
        for(const Operation &operation: operations)
        {
            SCOPED_TRACE(operation.normal);
            for(bool reflected: {false, true})
            {
                Value method = context.vm().float_class()->get_own_property(
                    interned_string(reflected ? operation.reflected
                                              : operation.normal));
                auto resolver = method.get_ptr<Function>()
                                    ->code_object.extract()
                                    ->trusted_handler_resolver;
                ASSERT_NE(nullptr, resolver);
                // Exercise both method registrations and every normalized
                // operand adaptation, including direct reflected methods.
                for(auto handler_order: {TrustedHandlerOperandOrder::Normal,
                                         TrustedHandlerOperandOrder::Reflected})
                {
                    for(bool integer_first: {false, true})
                    {
                        Value a =
                            integer_first ? integer.value() : number.value();
                        Value b =
                            integer_first ? number.value() : integer.value();
                        auto adapted =
                            resolver(&context.vm(), ShapeKey::from_value(a),
                                     ShapeKey::from_value(b), handler_order,
                                     TrustedHandlerArity::Binary);
                        ASSERT_TRUE(adapted.has_trusted_handler());
                        Owned<Value> result(
                            adapted.binary(context.thread(), a, b));
                        ASSERT_TRUE(has_native_layout<Float>(result.value()));
                        bool reverse_function =
                            reflected !=
                            (handler_order ==
                             TrustedHandlerOperandOrder::Reflected);
                        bool integer_on_left =
                            integer_first != reverse_function;
                        EXPECT_EQ(integer_on_left ? operation.reverse
                                                  : operation.forward,
                                  native_cast<Float>(result.value())->value());
                    }
                }

                Value left = reflected ? integer.value() : number.value();
                Value right = reflected ? number.value() : integer.value();
                auto order = reflected ? TrustedHandlerOperandOrder::Reflected
                                       : TrustedHandlerOperandOrder::Normal;
                auto resolution =
                    resolver(&context.vm(), ShapeKey::from_value(left),
                             ShapeKey::from_value(right), order,
                             TrustedHandlerArity::Binary);
                ASSERT_TRUE(resolution.has_trusted_handler());
                auto metadata = context.vm().trusted_handler_metadata(
                    erase_trusted_handler_target(resolution.binary));
                ASSERT_TRUE(metadata.has_value());
                EXPECT_EQ(TrustedHandlerEffects::Allocate |
                              TrustedHandlerEffects::Raise,
                          metadata->effects);
                EXPECT_EQ(operation.semantics, metadata->semantics);
                Owned<TValue<Function>> function(
                    TValue<Function>::from_value_assumed(
                        context.run_file(operation.source)));
                for(int replay = 0; replay < 3; ++replay)
                {
                    Owned<Value> direct(
                        resolution.binary(context.thread(), left, right));
                    ASSERT_TRUE(has_native_layout<Float>(direct.value()));
                    EXPECT_EQ(reflected ? operation.reverse : operation.forward,
                              native_cast<Float>(direct.value())->value());
                    Owned<Value> result(
                        context.thread()->call_clovervm_function(
                            function.value(), left, right));
                    ASSERT_TRUE(has_native_layout<Float>(result.value()));
                    EXPECT_EQ(reflected ? operation.reverse : operation.forward,
                              native_cast<Float>(result.value())->value());
                }
                const auto &caches = function.extract()
                                         ->code_object.extract()
                                         ->inline_caches.operator_caches;
                ASSERT_EQ(1u, caches.size());
                EXPECT_TRUE(caches[0].matches_reflectable_binary(left, right));
                ASSERT_FALSE(caches[0].trusted_handler.is_null());
                EXPECT_EQ(erase_trusted_handler_target(resolution.binary),
                          caches[0].trusted_handler.target(
                              TrustedHandlerArity::Binary));
                EXPECT_NE(nullptr, caches[0].operand_lookup_validity_cells[0]);
                EXPECT_NE(nullptr, caches[0].operand_lookup_validity_cells[1]);
                // The successful handler remains cached for an overflowing
                // integer of the same shape. Failure must propagate on replay.
                for(int replay = 0; replay < 2; ++replay)
                {
                    Value failed = context.thread()->call_clovervm_function(
                        function.value(),
                        reflected ? huge.value() : number.value(),
                        reflected ? number.value() : huge.value());
                    EXPECT_TRUE(failed.is_exception_marker());
                    EXPECT_TRUE(context.thread()->has_pending_exception());
                    context.thread()->clear_pending_exception();
                }

                EXPECT_FALSE(resolver(&context.vm(), ShapeKey::from_value(left),
                                      ShapeKey::from_value(right), order,
                                      TrustedHandlerArity::Unary)
                                 .has_trusted_handler());
                EXPECT_FALSE(resolver(&context.vm(),
                                      ShapeKey::from_value(number.value()),
                                      ShapeKey::from_value(subclass.value()),
                                      order, TrustedHandlerArity::Binary)
                                 .has_trusted_handler());
            }
            // Existing float/SMI/bool targets keep their narrower effects.
            Value method = context.vm().float_class()->get_own_property(
                interned_string(operation.normal));
            auto resolver = method.get_ptr<Function>()
                                ->code_object.extract()
                                ->trusted_handler_resolver;
            for(Value right:
                {number.value(), Value::from_smi(1), Value::True()})
            {
                auto resolution = resolver(&context.vm(),
                                           ShapeKey::from_value(number.value()),
                                           ShapeKey::from_value(right),
                                           TrustedHandlerOperandOrder::Normal,
                                           TrustedHandlerArity::Binary);
                ASSERT_TRUE(resolution.has_trusted_handler());
                auto metadata = context.vm().trusted_handler_metadata(
                    erase_trusted_handler_target(resolution.binary));
                ASSERT_TRUE(metadata.has_value());
                EXPECT_EQ(operation.raises_without_heap_integer,
                          has_trusted_handler_effects(
                              metadata->effects, TrustedHandlerEffects::Raise));
            }
        }
    }

    TEST(FloatRuntime, EqualityTrustedHandlersPreserveIntegerPrecision)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        const struct Sample
        {
            double number;
            const wchar_t *integer;
            bool equal;
        } samples[] = {
            {0x1p53, L"9007199254740993", false},
            {0x1p60, L"2**60", true},
            {-0x1p60, L"-(2**60)", true},
            {0x1p60, L"2**60 + 1", false},
            {0x1p100, L"2**100", true},
            {0x1.fffffffffffffp1023, L"(2**53 - 1) * 2**971", true},
            {0.0, L"False", true},
            {-0.0, L"0", true},
            {1.0, L"True", true},
            {0x1p-1074, L"0", false},
            {std::numeric_limits<double>::infinity(), L"2**2000", false},
            {std::numeric_limits<double>::quiet_NaN(), L"0", false},
        };
        for(const wchar_t *method_name: {L"__eq__", L"__ne__"})
        {
            bool not_equal = std::wstring_view(method_name) == L"__ne__";
            Value method = context.vm().float_class()->get_own_property(
                interned_string(method_name));
            auto resolver = method.get_ptr<Function>()
                                ->code_object.extract()
                                ->trusted_handler_resolver;
            ASSERT_NE(nullptr, resolver);
            Owned<TValue<Function>> comparison_function(
                TValue<Function>::from_value_assumed(context.run_file(
                    not_equal ? L"def compare(left, right):\n"
                                L"    return left != right\ncompare\n"
                              : L"def compare(left, right):\n"
                                L"    return left == right\ncompare\n")));
            CodeObject *comparison =
                comparison_function.extract()->code_object.extract();
            for(const Sample &sample: samples)
            {
                SCOPED_TRACE(sample.integer);
                Owned<Value> integer(context.run_file(sample.integer));
                ASSERT_FALSE(integer.value().is_exception_marker());
                Owned<Value> number(box_float(context.thread(), sample.number));
                for(auto order: {TrustedHandlerOperandOrder::Normal,
                                 TrustedHandlerOperandOrder::Reflected})
                {
                    Value left = order == TrustedHandlerOperandOrder::Normal
                                     ? number.value()
                                     : integer.value();
                    Value right = order == TrustedHandlerOperandOrder::Normal
                                      ? integer.value()
                                      : number.value();
                    auto resolution =
                        resolver(&context.vm(), ShapeKey::from_value(left),
                                 ShapeKey::from_value(right), order,
                                 TrustedHandlerArity::Binary);
                    ASSERT_TRUE(resolution.has_trusted_handler());
                    Value expected = sample.equal != not_equal ? Value::True()
                                                               : Value::False();
                    for(int replay = 0; replay < 3; ++replay)
                    {
                        EXPECT_EQ(expected, resolution.binary(context.thread(),
                                                              left, right));
                    }
                    for(int replay = 0; replay < 3; ++replay)
                    {
                        EXPECT_EQ(
                            expected,
                            context.thread()->call_clovervm_function(
                                comparison_function.value(), left, right));
                    }
                    ASSERT_EQ(1u,
                              comparison->inline_caches.operator_caches.size());
                    const auto &cache =
                        comparison->inline_caches.operator_caches[0];
                    EXPECT_TRUE(cache.matches_reflectable_binary(left, right));
                    EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
                    EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
                    if(order == TrustedHandlerOperandOrder::Normal)
                    {
                        ASSERT_FALSE(cache.trusted_handler.is_null());
                        EXPECT_EQ(
                            erase_trusted_handler_target(resolution.binary),
                            cache.trusted_handler.target(
                                TrustedHandlerArity::Binary));
                    }
                    else
                    {
                        // Int comparisons have no trusted resolver. Their
                        // cached call returns NotImplemented before the float
                        // handler runs through the existing continuation.
                        Value int_method =
                            context.vm().int_class()->get_own_property(
                                interned_string(method_name));
                        EXPECT_TRUE(cache.trusted_handler.is_null());
                        EXPECT_EQ(int_method.get_ptr<Function>(),
                                  cache.function);
                    }
                    auto metadata = context.vm().trusted_handler_metadata(
                        erase_trusted_handler_target(resolution.binary));
                    ASSERT_TRUE(metadata.has_value());
                    EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
                    EXPECT_EQ(not_equal ? TrustedHandlerSemantics::NotEqual
                                        : TrustedHandlerSemantics::Equal,
                              metadata->semantics);
                }
            }
            Owned<Value> int_subclass(
                context.run_file(L"class I(int): pass\nI(2**60)\n"));
            Owned<Value> float_subclass(
                context.run_file(L"class F(float): pass\nF(2**60)\n"));
            Owned<Value> number(box_float(context.thread(), 0x1p60));
            for(Value subclass: {int_subclass.value(), float_subclass.value()})
            {
                for(auto order: {TrustedHandlerOperandOrder::Normal,
                                 TrustedHandlerOperandOrder::Reflected})
                {
                    EXPECT_FALSE(resolver(&context.vm(),
                                          ShapeKey::from_value(number.value()),
                                          ShapeKey::from_value(subclass), order,
                                          TrustedHandlerArity::Binary)
                                     .has_trusted_handler());
                }
            }
            EXPECT_FALSE(resolver(&context.vm(),
                                  ShapeKey::from_value(number.value()),
                                  ShapeKey::from_value(Value::True()),
                                  TrustedHandlerOperandOrder::Normal,
                                  TrustedHandlerArity::Unary)
                             .has_trusted_handler());
        }
    }

    TEST(FloatRuntime, OrderingTrustedHandlersPreserveIntegerPrecision)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        struct Sample
        {
            double number;
            const wchar_t *integer;
            int comparison;
        };
        const Sample samples[] = {
            {0x1p53, L"9007199254740993", -1},
            {0x1p60, L"2**60", 0},
            {-0x1p60, L"-(2**60) - 1", 1},
            {0x1p60, L"2**60 + 1", -1},
            {0x1.fffffffffffffp1023, L"(2**53 - 1) * 2**971", 0},
            {0.0, L"False", 0},
            {-0.0, L"0", 0},
            {1.0, L"True", 0},
            {1.5, L"1", 1},
            {-1.5, L"-1", -1},
            {0x1p-1074, L"0", 1},
            {std::numeric_limits<double>::infinity(), L"2**2000", 1},
            {-std::numeric_limits<double>::infinity(), L"-(2**2000)", -1},
            {std::numeric_limits<double>::quiet_NaN(), L"0", 2},
        };
        struct Method
        {
            const wchar_t *name;
            const wchar_t *forward;
            const wchar_t *reverse;
            TrustedHandlerSemantics semantics;
            TrustedHandlerSemantics reflected_semantics;
        };
        const Method methods[] = {
            {L"__lt__", L"<", L">", TrustedHandlerSemantics::Less,
             TrustedHandlerSemantics::Greater},
            {L"__le__", L"<=", L">=", TrustedHandlerSemantics::LessEqual,
             TrustedHandlerSemantics::GreaterEqual},
            {L"__gt__", L">", L"<", TrustedHandlerSemantics::Greater,
             TrustedHandlerSemantics::Less},
            {L"__ge__", L">=", L"<=", TrustedHandlerSemantics::GreaterEqual,
             TrustedHandlerSemantics::LessEqual},
        };
        for(const Method &method: methods)
        {
            auto resolver = context.vm()
                                .float_class()
                                ->get_own_property(interned_string(method.name))
                                .get_ptr<Function>()
                                ->code_object.extract()
                                ->trusted_handler_resolver;
            ASSERT_NE(nullptr, resolver);
            for(const Sample &sample: samples)
            {
                SCOPED_TRACE(sample.integer);
                Owned<Value> integer(context.run_file(sample.integer));
                Owned<Value> number(box_float(context.thread(), sample.number));
                ASSERT_FALSE(integer.value().is_exception_marker());
                bool result = false;
                if(sample.comparison != 2)
                {
                    switch(method.semantics)
                    {
                        case TrustedHandlerSemantics::Less:
                            result = sample.comparison < 0;
                            break;
                        case TrustedHandlerSemantics::LessEqual:
                            result = sample.comparison <= 0;
                            break;
                        case TrustedHandlerSemantics::Greater:
                            result = sample.comparison > 0;
                            break;
                        case TrustedHandlerSemantics::GreaterEqual:
                            result = sample.comparison >= 0;
                            break;
                        default:
                            FAIL();
                    }
                }
                Value expected = result ? Value::True() : Value::False();
                for(auto order: {TrustedHandlerOperandOrder::Normal,
                                 TrustedHandlerOperandOrder::Reflected})
                {
                    bool reflected =
                        order == TrustedHandlerOperandOrder::Reflected;
                    Value left = reflected ? integer.value() : number.value();
                    Value right = reflected ? number.value() : integer.value();
                    auto resolution =
                        resolver(&context.vm(), ShapeKey::from_value(left),
                                 ShapeKey::from_value(right), order,
                                 TrustedHandlerArity::Binary);
                    ASSERT_TRUE(resolution.has_trusted_handler());
                    auto metadata = context.vm().trusted_handler_metadata(
                        erase_trusted_handler_target(resolution.binary));
                    ASSERT_TRUE(metadata.has_value());
                    EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
                    EXPECT_EQ(reflected ? method.reflected_semantics
                                        : method.semantics,
                              metadata->semantics);
                    for(int replay = 0; replay < 3; ++replay)
                        EXPECT_EQ(expected, resolution.binary(context.thread(),
                                                              left, right));
                    std::wstring source =
                        L"def compare(left, right):\n    return left ";
                    source += reflected ? method.reverse : method.forward;
                    source += L" right\ncompare\n";
                    Owned<TValue<Function>> function(
                        TValue<Function>::from_value_assumed(
                            context.run_file(source.c_str())));
                    for(int replay = 0; replay < 3; ++replay)
                        EXPECT_EQ(expected,
                                  context.thread()->call_clovervm_function(
                                      function.value(), left, right));
                    const auto &caches = function.extract()
                                             ->code_object.extract()
                                             ->inline_caches.operator_caches;
                    ASSERT_EQ(1u, caches.size());
                    EXPECT_TRUE(
                        caches[0].matches_reflectable_binary(left, right));
                    EXPECT_NE(nullptr,
                              caches[0].operand_lookup_validity_cells[0]);
                    EXPECT_NE(nullptr,
                              caches[0].operand_lookup_validity_cells[1]);
                    // Int methods keep their existing NotImplemented
                    // continuation.
                    EXPECT_EQ(reflected, caches[0].trusted_handler.is_null());
                    if(!reflected)
                        EXPECT_EQ(
                            erase_trusted_handler_target(resolution.binary),
                            caches[0].trusted_handler.target(
                                TrustedHandlerArity::Binary));
                }
            }
            Owned<Value> integer_subclass(
                context.run_file(L"class I(int): pass\nI(2**60)\n"));
            Owned<Value> float_subclass(
                context.run_file(L"class F(float): pass\nF(2**60)\n"));
            Owned<Value> number(box_float(context.thread(), 0x1p60));
            for(Value subclass: {integer_subclass.value(),
                                 float_subclass.value(), Value::None()})
            {
                for(auto order: {TrustedHandlerOperandOrder::Normal,
                                 TrustedHandlerOperandOrder::Reflected})
                {
                    EXPECT_FALSE(resolver(&context.vm(),
                                          ShapeKey::from_value(number.value()),
                                          ShapeKey::from_value(subclass), order,
                                          TrustedHandlerArity::Binary)
                                     .has_trusted_handler());
                    EXPECT_FALSE(resolver(&context.vm(),
                                          ShapeKey::from_value(subclass),
                                          ShapeKey::from_value(number.value()),
                                          order, TrustedHandlerArity::Binary)
                                     .has_trusted_handler());
                }
            }
            EXPECT_FALSE(resolver(&context.vm(),
                                  ShapeKey::from_value(number.value()),
                                  ShapeKey::from_value(Value::True()),
                                  TrustedHandlerOperandOrder::Normal,
                                  TrustedHandlerArity::Unary)
                             .has_trusted_handler());
        }
    }

    TEST(FloatRuntime, BoxFloatCreatesFreshExactFloat)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());

        Owned<Value> first(box_float(context.thread(), 2.5));
        Owned<Value> second(box_float(context.thread(), 2.5));

        ASSERT_TRUE(has_native_layout<Float>(first.raw_value()));
        ASSERT_TRUE(has_native_layout<Float>(second.raw_value()));
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
        ASSERT_TRUE(has_native_layout<Float>(result.value()));
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

    TEST(FloatRuntime, NanHashIsCanonicalAndSupportsDistinctDictionaryKeys)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        CodeObject *code = context.compile_file(LR"(
def hash_one(value):
    return hash(value)
nan_hash = 208141517942204353
for value in (False, True, 0.0, -0.0, float('inf'), -float('inf')):
    assert hash_one(value) != nan_hash
for value in range(-16, 17):
    assert hash_one(value) != nan_hash
for value in (first, second, float('nan'), float('inf') - float('inf')):
    assert hash_one(value) == nan_hash
    assert hash_one(value) == nan_hash
    assert float.__hash__(value) == nan_hash
    assert value != value
class F(float):
    pass
assert hash(F(second)) == nan_hash
assert first is not second
values = {first: 1, second: 2}
assert len(values) == 2
assert values[first] == 1
assert values[second] == 2
assert float('nan') not in values
values[first] = 3
assert len(values) == 2
assert values[first] == 3
del values[second]
assert first in values
assert second not in values
keys = []
for i in range(40):
    keys.append(float('nan'))
values = {}
for i in range(len(keys)):
    values[keys[i]] = i
assert len(values) == len(keys)
for i in range(len(keys)):
    assert values[keys[i]] == i
assert hash((first,)) == hash((second,))
assert hash(((first,),)) == hash(((second,),))
values = {(first,): 1, (second,): 2}
assert len(values) == 2
assert values[(first,)] == 1
assert values[(second,)] == 2
True
)");
        const uint64_t nan_patterns[] = {
            0x7ff8000000000000ULL, 0x7ff8000000000001ULL, 0x7fffffffffffffffULL,
            0xfff8000000000000ULL, 0xfff8000000000001ULL, 0x7ff0000000000001ULL,
            0xfff0000000000001ULL};
        for(uint64_t bits: nan_patterns)
        {
            for(const wchar_t *name: {L"first", L"second"})
            {
                double number = name == std::wstring_view(L"first")
                                    ? std::numeric_limits<double>::quiet_NaN()
                                    : std::bit_cast<double>(bits);
                ASSERT_TRUE(code->get_defining_module()
                                .extract()
                                ->set_own_property(
                                    interned_string(name),
                                    context.thread()
                                        ->make_object_value<Float>(number)
                                        .raw_value())
                                .value());
            }
            EXPECT_EQ(Value::True(),
                      context.thread()->run_clovervm_code_object(code));
        }
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
        ASSERT_TRUE(has_native_layout<Function>(method));
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
