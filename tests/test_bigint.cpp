#include "builtin_types/bigint.h"

#include "builtin_types/int.h"
#include "builtin_types/tuple.h"
#include "object_model/owned.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <limits>
#include <string>

using namespace cl;

namespace
{
    void expect_smi_view(int64_t value, signum_t expected_signum,
                         size_t expected_n_digits, digit_t expected_digit0,
                         digit_t expected_digit1)
    {
        SmiViewStorage storage;
        ConstBigIntView view = smi_bigint_view(value, &storage);

        EXPECT_EQ(expected_signum, view.signum);
        EXPECT_EQ(expected_n_digits, view.n_digits);
        EXPECT_EQ(expected_digit0, view.digits[0]);
        EXPECT_EQ(expected_digit1, view.digits[1]);
    }

    std::wstring int_value_decimal(Value value)
    {
        if(value.is_smi())
        {
            return std::to_wstring(value.get_smi());
        }
        BigInt *bigint = assume_convert_to<BigInt>(value);
        return bigint_to_decimal_string(bigint->view());
    }

    void expect_pending_exception(ThreadState *thread,
                                  const wchar_t *class_name,
                                  const wchar_t *message)
    {
        ASSERT_TRUE(thread->has_pending_exception());
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

TEST(BigInt, SmiViewZeroViewIsCanonical) { expect_smi_view(0, 0, 0, 0, 0); }

TEST(BigInt, SmiViewPositiveViewIsLittleEndian)
{
    expect_smi_view(1, 1, 1, 1, 0);
    expect_smi_view(int64_t{1} << kDigitBits, 1, 2, 0, 1);
}

TEST(BigInt, SmiViewNegativeViewUsesMagnitudeDigits)
{
    expect_smi_view(-1, -1, 1, 1, 0);
    expect_smi_view(-(int64_t{1} << kDigitBits), -1, 2, 0, 1);
}

TEST(BigInt, SmiViewBoundaryViewsFitInTwoDigits)
{
    double_digit_t max_magnitude = static_cast<double_digit_t>(value_smi_max);
    expect_smi_view(value_smi_max, 1, 2, static_cast<digit_t>(max_magnitude),
                    static_cast<digit_t>(max_magnitude >> kDigitBits));

    double_digit_t min_magnitude = double_digit_t(-(value_smi_min + 1)) + 1;
    expect_smi_view(value_smi_min, -1, 2, static_cast<digit_t>(min_magnitude),
                    static_cast<digit_t>(min_magnitude >> kDigitBits));
}

TEST(BigInt, ScratchStartsAsCanonicalZeroWithInlineStorage)
{
    BigIntScratch scratch(2);

    ConstBigIntView view = scratch.view();
    EXPECT_EQ(0u, view.n_digits);
    EXPECT_EQ(0, view.signum);

    MutableBigIntView mutable_view = scratch.mutable_view();
    EXPECT_EQ(2u, mutable_view.capacity);
    EXPECT_EQ(0u, mutable_view.n_digits);
    EXPECT_EQ(0, mutable_view.signum);
    ASSERT_NE(nullptr, mutable_view.digits);
}

TEST(BigInt, ScratchUsesOverflowStorageForLargeCapacity)
{
    BigIntScratch scratch(16);
    MutableBigIntView mutable_view = scratch.mutable_view();

    EXPECT_EQ(16u, mutable_view.capacity);
    EXPECT_EQ(0u, mutable_view.n_digits);
    ASSERT_NE(nullptr, mutable_view.digits);
}

TEST(BigInt, NormalizeTrimsHighZeroDigits)
{
    digit_t digits[] = {7, 0, 0};

    EXPECT_FALSE(is_normalized_bigint_view(ConstBigIntView{3, 1, digits}));
    ConstBigIntView view = normalize_bigint_view(ConstBigIntView{3, 1, digits});

    EXPECT_EQ(1u, view.n_digits);
    EXPECT_EQ(1, view.signum);
    EXPECT_EQ(digits, view.digits);
    EXPECT_TRUE(is_normalized_bigint_view(view));
}

TEST(BigInt, NormalizeCanonicalizesZero)
{
    digit_t digits[] = {0, 0};

    EXPECT_FALSE(is_normalized_bigint_view(ConstBigIntView{2, -1, digits}));
    ConstBigIntView view =
        normalize_bigint_view(ConstBigIntView{2, -1, digits});

    EXPECT_EQ(0u, view.n_digits);
    EXPECT_EQ(0, view.signum);
    EXPECT_EQ(digits, view.digits);
    EXPECT_TRUE(is_normalized_bigint_view(view));
}

TEST(BigInt, IntSubclassSmallValuesRemainHeapBigInts)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    for(int64_t value:
        {int64_t(0), int64_t(1), int64_t(-1), value_smi_min, value_smi_max})
    {
        std::wstring source =
            L"class I(int): pass\nx = I(" + std::to_wstring(value) + L")\nx\n";
        Owned<Value> result(context.run_file(source.c_str()));
        ASSERT_FALSE(result.value().is_exception_marker());
        ASSERT_TRUE(can_convert_to<BigInt>(result.value()));
        EXPECT_FALSE(result.value().is_smi());
        Expected<TValue<SMI>> converted =
            try_intlike_value_to_smi(result.value());
        ASSERT_TRUE(converted.has_value());
        EXPECT_EQ(value, converted.value().extract());
        EXPECT_EQ(value, bigint_to_smi(result.value().get_ptr<BigInt>()->view())
                             .value()
                             .extract());
        EXPECT_NE(context.vm().int_class(),
                  result.value().get_ptr<BigInt>()->get_shape()->get_class());
        EXPECT_EQ(0u, result.value()
                          .get_ptr<BigInt>()
                          ->get_shape()
                          ->get_inline_slot_capacity());
    }
}

TEST(BigInt, FinalizeReturnsSmiForSmallValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t digits[] = {123};

    Expected<Value> value =
        finalize_bigint(context.thread(), ConstBigIntView{1, 1, digits});

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(Value::from_smi(123), value.value());
}

TEST(BigInt, BigIntToSmiConvertsSmallValue)
{
    digit_t digits[] = {123};

    Expected<TValue<SMI>> value = bigint_to_smi(ConstBigIntView{1, 1, digits});

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(Value::from_smi(123), value.value().raw_value());
}

TEST(BigInt, BigIntToSmiRejectsOverflow)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    double_digit_t magnitude = static_cast<double_digit_t>(value_smi_max) + 1;
    digit_t digits[] = {static_cast<digit_t>(magnitude),
                        static_cast<digit_t>(magnitude >> kDigitBits)};

    Expected<TValue<SMI>> value = bigint_to_smi(ConstBigIntView{2, 1, digits});

    EXPECT_TRUE(value.has_exception());
    expect_pending_exception(context.thread(), L"OverflowError",
                             L"integer overflow");
}

TEST(BigInt, FinalizeReturnsHeapBigIntForNonSmiMagnitude)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    double_digit_t magnitude = static_cast<double_digit_t>(value_smi_max) + 1;
    digit_t digits[] = {static_cast<digit_t>(magnitude),
                        static_cast<digit_t>(magnitude >> kDigitBits)};

    Expected<Value> value =
        finalize_bigint(context.thread(), ConstBigIntView{2, 1, digits});

    ASSERT_TRUE(value.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(value.value()));
    BigInt *bigint = assume_convert_to<BigInt>(value.value());
    ConstBigIntView view = bigint->view();
    EXPECT_EQ(2u, view.n_digits);
    EXPECT_EQ(1, view.signum);
    EXPECT_EQ(digits[0], view.digits[0]);
    EXPECT_EQ(digits[1], view.digits[1]);
    EXPECT_NE(digits, view.digits);
}

TEST(BigInt, BigIntFromInt64FinalizesToSmiWhenPossible)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<Value> value =
        int_value_from_int64(context.thread(), value_smi_min);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(Value::from_smi(value_smi_min), value.value());
}

TEST(BigInt, BigIntFromInt64CanProduceHeapBigInt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<Value> value = int_value_from_int64(
        context.thread(), std::numeric_limits<int64_t>::min());

    ASSERT_TRUE(value.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(value.value()));
    BigInt *bigint = assume_convert_to<BigInt>(value.value());
    ConstBigIntView view = bigint->view();
    EXPECT_EQ(2u, view.n_digits);
    EXPECT_EQ(-1, view.signum);
    EXPECT_EQ(0u, view.digits[0]);
    EXPECT_EQ(0x80000000u, view.digits[1]);
}

TEST(BigInt, BigIntToInt64ConvertsSignedBoundaryValues)
{
    digit_t max_digits[] = {0xffffffffu, 0x7fffffffu};
    digit_t min_digits[] = {0, 0x80000000u};

    Expected<int64_t> max = bigint_to_int64(ConstBigIntView{2, 1, max_digits});
    Expected<int64_t> min = bigint_to_int64(ConstBigIntView{2, -1, min_digits});

    ASSERT_TRUE(max.has_value());
    ASSERT_TRUE(min.has_value());
    EXPECT_EQ(std::numeric_limits<int64_t>::max(), max.value());
    EXPECT_EQ(std::numeric_limits<int64_t>::min(), min.value());
}

TEST(BigInt, BigIntToInt64RejectsOverflow)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t digits[] = {0, 0, 1};

    Expected<int64_t> value = bigint_to_int64(ConstBigIntView{3, 1, digits});

    EXPECT_TRUE(value.has_exception());
    expect_pending_exception(context.thread(), L"OverflowError",
                             L"integer overflow");
}

TEST(BigInt, BigIntToDoubleConvertsExactValues)
{
    digit_t zero[] = {0};
    digit_t one[] = {1};
    digit_t two_to_53[] = {0, 1u << 21};

    Expected<double> zero_result =
        bigint_to_double(ConstBigIntView{0, 0, zero});
    Expected<double> one_result = bigint_to_double(ConstBigIntView{1, 1, one});
    Expected<double> negative_one_result =
        bigint_to_double(ConstBigIntView{1, -1, one});
    Expected<double> two_to_53_result =
        bigint_to_double(ConstBigIntView{2, 1, two_to_53});

    ASSERT_TRUE(zero_result.has_value());
    ASSERT_TRUE(one_result.has_value());
    ASSERT_TRUE(negative_one_result.has_value());
    ASSERT_TRUE(two_to_53_result.has_value());
    EXPECT_DOUBLE_EQ(0.0, zero_result.value());
    EXPECT_DOUBLE_EQ(1.0, one_result.value());
    EXPECT_DOUBLE_EQ(-1.0, negative_one_result.value());
    EXPECT_DOUBLE_EQ(9007199254740992.0, two_to_53_result.value());
}

TEST(BigInt, BigIntToDoubleRoundsToNearestEven)
{
    digit_t tie_down[] = {1, 1u << 21};
    digit_t round_up[] = {3, 1u << 21};

    Expected<double> tie_down_result =
        bigint_to_double(ConstBigIntView{2, 1, tie_down});
    Expected<double> round_up_result =
        bigint_to_double(ConstBigIntView{2, 1, round_up});
    Expected<double> negative_round_up_result =
        bigint_to_double(ConstBigIntView{2, -1, round_up});

    ASSERT_TRUE(tie_down_result.has_value());
    ASSERT_TRUE(round_up_result.has_value());
    ASSERT_TRUE(negative_round_up_result.has_value());
    EXPECT_DOUBLE_EQ(9007199254740992.0, tie_down_result.value());
    EXPECT_DOUBLE_EQ(9007199254740996.0, round_up_result.value());
    EXPECT_DOUBLE_EQ(-9007199254740996.0, negative_round_up_result.value());
}

TEST(BigInt, BigIntToDoubleHandlesLargeFiniteAndOverflow)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t two_to_1023[32] = {};
    two_to_1023[31] = 0x80000000u;
    digit_t overflow_digits[32];
    std::fill_n(overflow_digits, 32, 0xffffffffu);

    Expected<double> finite =
        bigint_to_double(ConstBigIntView{32, 1, two_to_1023});
    Expected<double> negative_finite =
        bigint_to_double(ConstBigIntView{32, -1, two_to_1023});
    Expected<double> overflow =
        bigint_to_double(ConstBigIntView{32, 1, overflow_digits});

    ASSERT_TRUE(finite.has_value());
    ASSERT_TRUE(negative_finite.has_value());
    EXPECT_DOUBLE_EQ(8.98846567431158e307, finite.value());
    EXPECT_DOUBLE_EQ(-8.98846567431158e307, negative_finite.value());
    EXPECT_TRUE(overflow.has_exception());
    expect_pending_exception(context.thread(), L"OverflowError",
                             L"int too large to convert to float");
}

TEST(BigInt, BigIntNegateFinalizesToSmiOrHeapBigInt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t small_digits[] = {1};
    digit_t large_digits[] = {0, 0, 1};

    Expected<Value> small =
        bigint_negate(context.thread(), ConstBigIntView{1, -1, small_digits});
    Expected<Value> large =
        bigint_negate(context.thread(), ConstBigIntView{3, 1, large_digits});

    ASSERT_TRUE(small.has_value());
    EXPECT_EQ(Value::from_smi(1), small.value());
    ASSERT_TRUE(large.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(large.value()));
    BigInt *bigint = assume_convert_to<BigInt>(large.value());
    ConstBigIntView view = bigint->view();
    EXPECT_EQ(3u, view.n_digits);
    EXPECT_EQ(-1, view.signum);
    EXPECT_EQ(0u, view.digits[0]);
    EXPECT_EQ(0u, view.digits[1]);
    EXPECT_EQ(1u, view.digits[2]);
}

TEST(BigInt, DecimalStringFormatsZero)
{
    digit_t digits[] = {0};

    EXPECT_EQ(std::wstring(L"0"),
              bigint_to_decimal_string(ConstBigIntView{0, 0, digits}));
}

TEST(BigInt, DecimalStringFormatsSmiBoundaryMagnitude)
{
    double_digit_t magnitude = static_cast<double_digit_t>(value_smi_max);
    digit_t digits[] = {static_cast<digit_t>(magnitude),
                        static_cast<digit_t>(magnitude >> kDigitBits)};

    EXPECT_EQ(std::to_wstring(value_smi_max),
              bigint_to_decimal_string(ConstBigIntView{2, 1, digits}));
}

TEST(BigInt, DecimalStringFormatsInt64MinHeapBigInt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Expected<Value> value = int_value_from_int64(
        context.thread(), std::numeric_limits<int64_t>::min());
    ASSERT_TRUE(value.has_value());
    BigInt *bigint = assume_convert_to<BigInt>(value.value());

    EXPECT_EQ(std::to_wstring(std::numeric_limits<int64_t>::min()),
              bigint_to_decimal_string(bigint->view()));
}

TEST(BigInt, DecimalStringFormatsLargePositiveAndNegativeValues)
{
    digit_t digits[] = {0, 0, 1};

    EXPECT_EQ(std::wstring(L"18446744073709551616"),
              bigint_to_decimal_string(ConstBigIntView{3, 1, digits}));
    EXPECT_EQ(std::wstring(L"-18446744073709551616"),
              bigint_to_decimal_string(ConstBigIntView{3, -1, digits}));
}

TEST(BigInt, AbsMulAddU32WritesNormalizedMagnitude)
{
    digit_t digits[] = {0xffffffffu};
    BigIntScratch scratch(2);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_abs_mul_add_u32(&dest, ConstBigIntView{1, 1, digits}, 10, 9);

    EXPECT_EQ(2u, dest.n_digits);
    EXPECT_EQ(1, dest.signum);
    EXPECT_EQ(0xffffffffu, dest.digits[0]);
    EXPECT_EQ(9u, dest.digits[1]);
}

TEST(BigInt, AddPropagatesCarryToHeapBigInt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t left[] = {0xffffffffu, 0xffffffffu};
    digit_t right[] = {1};

    Expected<Value> result =
        bigint_add(context.thread(), ConstBigIntView{2, 1, left},
                   ConstBigIntView{1, 1, right});

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(result.value()));
    ConstBigIntView view = assume_convert_to<BigInt>(result.value())->view();
    EXPECT_EQ(3u, view.n_digits);
    EXPECT_EQ(1, view.signum);
    EXPECT_EQ(0u, view.digits[0]);
    EXPECT_EQ(0u, view.digits[1]);
    EXPECT_EQ(1u, view.digits[2]);
}

TEST(BigInt, SubBorrowsAndDemotesTrimmedResult)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t left[] = {0, 1};
    digit_t right[] = {1};

    Expected<Value> result =
        bigint_sub(context.thread(), ConstBigIntView{2, 1, left},
                   ConstBigIntView{1, 1, right});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(Value::from_smi(0xffffffffLL), result.value());
}

TEST(BigInt, AddCombinesSignedMagnitudes)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t large[] = {5, 1};
    digit_t small[] = {7};

    Expected<Value> positive =
        bigint_add(context.thread(), ConstBigIntView{2, 1, large},
                   ConstBigIntView{1, -1, small});
    Expected<Value> negative =
        bigint_add(context.thread(), ConstBigIntView{1, 1, small},
                   ConstBigIntView{2, -1, large});

    ASSERT_TRUE(positive.has_value());
    ASSERT_TRUE(negative.has_value());
    EXPECT_EQ(Value::from_smi(0xfffffffeLL), positive.value());
    EXPECT_EQ(Value::from_smi(-0xfffffffeLL), negative.value());
}

TEST(BigInt, AddCanonicalizesCancellationToZero)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t left[] = {3, 2};
    digit_t right[] = {3, 2};

    Expected<Value> result =
        bigint_add(context.thread(), ConstBigIntView{2, 1, left},
                   ConstBigIntView{2, -1, right});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(Value::from_smi(0), result.value());
}

TEST(BigInt, SubUsesSignedRightOperand)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t left[] = {1};
    digit_t right[] = {2};

    Expected<Value> result =
        bigint_sub(context.thread(), ConstBigIntView{1, 1, left},
                   ConstBigIntView{1, 1, right});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(Value::from_smi(-1), result.value());
}

TEST(BigInt, MulCanonicalizesZero)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t zero[] = {0};
    digit_t value[] = {7, 1};

    Expected<Value> result =
        bigint_mul(context.thread(), ConstBigIntView{0, 0, zero},
                   ConstBigIntView{2, -1, value});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(Value::from_smi(0), result.value());
}

TEST(BigInt, MulTrimsUnusedHighDigit)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t left[] = {1};
    digit_t right[] = {1};

    Expected<Value> result =
        bigint_mul(context.thread(), ConstBigIntView{1, -1, left},
                   ConstBigIntView{1, -1, right});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(Value::from_smi(1), result.value());
}

TEST(BigInt, MulPropagatesSingleDigitCarry)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t left[] = {0xffffffffu};
    digit_t right[] = {0xffffffffu};

    Expected<Value> result =
        bigint_mul(context.thread(), ConstBigIntView{1, -1, left},
                   ConstBigIntView{1, 1, right});

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(result.value()));
    ConstBigIntView view = assume_convert_to<BigInt>(result.value())->view();
    EXPECT_EQ(2u, view.n_digits);
    EXPECT_EQ(-1, view.signum);
    EXPECT_EQ(1u, view.digits[0]);
    EXPECT_EQ(0xfffffffeu, view.digits[1]);
}

TEST(BigInt, MulAccumulatesMultiDigitProducts)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t left[] = {0xffffffffu, 1};
    digit_t right[] = {0xffffffffu, 1};

    Expected<Value> result =
        bigint_mul(context.thread(), ConstBigIntView{2, 1, left},
                   ConstBigIntView{2, -1, right});

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(can_convert_to<BigInt>(result.value()));
    ConstBigIntView view = assume_convert_to<BigInt>(result.value())->view();
    EXPECT_EQ(3u, view.n_digits);
    EXPECT_EQ(-1, view.signum);
    EXPECT_EQ(1u, view.digits[0]);
    EXPECT_EQ(0xfffffffcu, view.digits[1]);
    EXPECT_EQ(3u, view.digits[2]);
}

TEST(BigInt, FloorDivUsesPythonSignedSemantics)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t large[] = {0, 0, 1};
    digit_t three[] = {3};

    Expected<Value> positive =
        bigint_floor_div(context.thread(), ConstBigIntView{3, 1, large},
                         ConstBigIntView{1, 1, three});
    Expected<Value> negative_dividend =
        bigint_floor_div(context.thread(), ConstBigIntView{3, -1, large},
                         ConstBigIntView{1, 1, three});
    Expected<Value> negative_divisor =
        bigint_floor_div(context.thread(), ConstBigIntView{3, 1, large},
                         ConstBigIntView{1, -1, three});
    Expected<Value> both_negative =
        bigint_floor_div(context.thread(), ConstBigIntView{3, -1, large},
                         ConstBigIntView{1, -1, three});

    ASSERT_TRUE(positive.has_value());
    ASSERT_TRUE(negative_dividend.has_value());
    ASSERT_TRUE(negative_divisor.has_value());
    ASSERT_TRUE(both_negative.has_value());
    EXPECT_EQ(L"6148914691236517205", int_value_decimal(positive.value()));
    EXPECT_EQ(L"-6148914691236517206",
              int_value_decimal(negative_dividend.value()));
    EXPECT_EQ(L"-6148914691236517206",
              int_value_decimal(negative_divisor.value()));
    EXPECT_EQ(L"6148914691236517205", int_value_decimal(both_negative.value()));
}

TEST(BigInt, ModUsesDivisorSign)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t large[] = {0, 0, 1};
    digit_t three[] = {3};

    Expected<Value> positive =
        bigint_mod(context.thread(), ConstBigIntView{3, 1, large},
                   ConstBigIntView{1, 1, three});
    Expected<Value> negative_dividend =
        bigint_mod(context.thread(), ConstBigIntView{3, -1, large},
                   ConstBigIntView{1, 1, three});
    Expected<Value> negative_divisor =
        bigint_mod(context.thread(), ConstBigIntView{3, 1, large},
                   ConstBigIntView{1, -1, three});
    Expected<Value> both_negative =
        bigint_mod(context.thread(), ConstBigIntView{3, -1, large},
                   ConstBigIntView{1, -1, three});

    ASSERT_TRUE(positive.has_value());
    ASSERT_TRUE(negative_dividend.has_value());
    ASSERT_TRUE(negative_divisor.has_value());
    ASSERT_TRUE(both_negative.has_value());
    EXPECT_EQ(Value::from_smi(1), positive.value());
    EXPECT_EQ(Value::from_smi(2), negative_dividend.value());
    EXPECT_EQ(Value::from_smi(-2), negative_divisor.value());
    EXPECT_EQ(Value::from_smi(-1), both_negative.value());
}

TEST(BigInt, FloorDivAndModRejectZeroDivisor)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t value[] = {1};
    digit_t zero[] = {0};

    Expected<Value> quotient =
        bigint_floor_div(context.thread(), ConstBigIntView{1, 1, value},
                         ConstBigIntView{0, 0, zero});
    ASSERT_TRUE(quotient.has_exception());
    expect_pending_exception(context.thread(), L"ZeroDivisionError",
                             L"division by zero");
    context.thread()->clear_pending_exception();

    Expected<Value> remainder =
        bigint_mod(context.thread(), ConstBigIntView{1, 1, value},
                   ConstBigIntView{0, 0, zero});
    ASSERT_TRUE(remainder.has_exception());
    expect_pending_exception(context.thread(), L"ZeroDivisionError",
                             L"division by zero");
}

TEST(BigInt, FloorDivAndModDemoteExactSmallResults)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t large[] = {0, 0, 1};

    Expected<Value> quotient =
        bigint_floor_div(context.thread(), ConstBigIntView{3, 1, large},
                         ConstBigIntView{3, 1, large});
    Expected<Value> remainder =
        bigint_mod(context.thread(), ConstBigIntView{3, 1, large},
                   ConstBigIntView{3, 1, large});

    ASSERT_TRUE(quotient.has_value());
    ASSERT_TRUE(remainder.has_value());
    EXPECT_EQ(Value::from_smi(1), quotient.value());
    EXPECT_EQ(Value::from_smi(0), remainder.value());
}

TEST(BigInt, LeftShiftPromotesAndPreservesSign)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t positive[] = {1};
    digit_t negative[] = {1};

    Expected<Value> positive_result =
        bigint_lshift(context.thread(), ConstBigIntView{1, 1, positive}, 64);
    Expected<Value> negative_result =
        bigint_lshift(context.thread(), ConstBigIntView{1, -1, negative}, 33);

    ASSERT_TRUE(positive_result.has_value());
    ASSERT_TRUE(negative_result.has_value());
    EXPECT_EQ(L"18446744073709551616",
              int_value_decimal(positive_result.value()));
    EXPECT_EQ(L"-8589934592", int_value_decimal(negative_result.value()));
}

TEST(BigInt, RightShiftUsesArithmeticFloorSemantics)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t positive[] = {0, 0, 1};
    digit_t negative_exact[] = {0, 0, 1};
    digit_t negative_round[] = {1, 0, 1};

    Expected<Value> positive_result =
        bigint_rshift(context.thread(), ConstBigIntView{3, 1, positive}, 64);
    Expected<Value> negative_exact_result = bigint_rshift(
        context.thread(), ConstBigIntView{3, -1, negative_exact}, 64);
    Expected<Value> negative_round_result = bigint_rshift(
        context.thread(), ConstBigIntView{3, -1, negative_round}, 64);
    Expected<Value> negative_large_shift = bigint_rshift(
        context.thread(), ConstBigIntView{3, -1, negative_round}, 200);

    ASSERT_TRUE(positive_result.has_value());
    ASSERT_TRUE(negative_exact_result.has_value());
    ASSERT_TRUE(negative_round_result.has_value());
    ASSERT_TRUE(negative_large_shift.has_value());
    EXPECT_EQ(Value::from_smi(1), positive_result.value());
    EXPECT_EQ(Value::from_smi(-1), negative_exact_result.value());
    EXPECT_EQ(Value::from_smi(-2), negative_round_result.value());
    EXPECT_EQ(Value::from_smi(-1), negative_large_shift.value());
}

TEST(BigInt, InvertUsesInfiniteTwosComplementSemantics)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t positive[] = {0, 0, 1};
    digit_t negative[] = {0, 0, 1};

    Expected<Value> positive_result =
        bigint_invert(context.thread(), ConstBigIntView{3, 1, positive});
    Expected<Value> negative_result =
        bigint_invert(context.thread(), ConstBigIntView{3, -1, negative});

    ASSERT_TRUE(positive_result.has_value());
    ASSERT_TRUE(negative_result.has_value());
    EXPECT_EQ(L"-18446744073709551617",
              int_value_decimal(positive_result.value()));
    EXPECT_EQ(L"18446744073709551615",
              int_value_decimal(negative_result.value()));
}

TEST(BigInt, BitwiseOpsUseInfiniteTwosComplementSemantics)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    digit_t two_to_64[] = {0, 0, 1};
    digit_t low_mask[] = {255};
    digit_t negative_mask[] = {255};
    digit_t small[] = {3};

    Expected<Value> or_positive =
        bigint_or(context.thread(), ConstBigIntView{3, 1, two_to_64},
                  ConstBigIntView{1, 1, small});
    Expected<Value> and_mixed =
        bigint_and(context.thread(), ConstBigIntView{3, -1, two_to_64},
                   ConstBigIntView{1, 1, low_mask});
    Expected<Value> or_mixed =
        bigint_or(context.thread(), ConstBigIntView{3, -1, two_to_64},
                  ConstBigIntView{1, 1, low_mask});
    Expected<Value> xor_mixed =
        bigint_xor(context.thread(), ConstBigIntView{3, -1, two_to_64},
                   ConstBigIntView{1, 1, low_mask});
    Expected<Value> and_negative =
        bigint_and(context.thread(), ConstBigIntView{3, -1, two_to_64},
                   ConstBigIntView{1, -1, negative_mask});

    ASSERT_TRUE(or_positive.has_value());
    ASSERT_TRUE(and_mixed.has_value());
    ASSERT_TRUE(or_mixed.has_value());
    ASSERT_TRUE(xor_mixed.has_value());
    ASSERT_TRUE(and_negative.has_value());
    EXPECT_EQ(L"18446744073709551619", int_value_decimal(or_positive.value()));
    EXPECT_EQ(Value::from_smi(0), and_mixed.value());
    EXPECT_EQ(L"-18446744073709551361", int_value_decimal(or_mixed.value()));
    EXPECT_EQ(L"-18446744073709551361", int_value_decimal(xor_mixed.value()));
    EXPECT_EQ(L"-18446744073709551616",
              int_value_decimal(and_negative.value()));
}

TEST(BigInt, CompareBigIntViewsUsesSignedMagnitude)
{
    digit_t one[] = {1};
    digit_t two[] = {2};
    digit_t high[] = {0, 1};

    EXPECT_EQ(0, compare_bigint(ConstBigIntView{1, 1, one},
                                ConstBigIntView{1, 1, one}));
    EXPECT_LT(
        compare_bigint(ConstBigIntView{1, 1, one}, ConstBigIntView{1, 1, two}),
        0);
    EXPECT_GT(
        compare_bigint(ConstBigIntView{2, 1, high}, ConstBigIntView{1, 1, two}),
        0);
    EXPECT_LT(compare_bigint(ConstBigIntView{1, -1, two},
                             ConstBigIntView{1, -1, one}),
              0);
    EXPECT_LT(
        compare_bigint(ConstBigIntView{1, -1, one}, ConstBigIntView{0, 0, one}),
        0);
    EXPECT_GT(
        compare_bigint(ConstBigIntView{1, 1, one}, ConstBigIntView{0, 0, one}),
        0);
}

TEST(BigInt, IntlikeValueToSmiAcceptsInlineIntegersAndBool)
{
    for(Value input:
        {Value::from_smi(-7), Value::from_smi(0),
         Value::from_smi(value_smi_max), Value::False(), Value::True()})
    {
        Expected<TValue<SMI>> result = try_intlike_value_to_smi(input);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(input.is_bool() ? (input == Value::True() ? 1 : 0)
                                  : input.get_smi(),
                  result.value().extract());
    }
}

TEST(BigInt, IntlikeValueToSmiRejectsNonIntegersWithRequestedMessage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    EXPECT_TRUE(try_intlike_value_to_smi(Value::None()).has_exception());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"expected an integer");
    context.thread()->clear_pending_exception();
    EXPECT_TRUE(try_intlike_value_to_smi(Value::None(), L"custom integer error")
                    .has_exception());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"custom integer error");
}

TEST(BigInt, ExactIntValueToSmiExcludesBool)
{
    TValue<SMI> bool_smi = TValue<SMI>::from_smi(7);
    Expected<IntToSmiStatus> bool_result =
        try_exact_int_value_to_smi(Value::True(), &bool_smi);

    ASSERT_TRUE(bool_result.has_value());
    EXPECT_EQ(IntToSmiStatus::NotInt, bool_result.value());
    EXPECT_EQ(7, bool_smi.extract());
}

TEST(BigInt, BigIntFromUint64PreservesUnsignedBoundaries)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    const uint64_t inputs[] = {0,
                               1,
                               static_cast<uint64_t>(value_smi_max),
                               static_cast<uint64_t>(value_smi_max) + 1,
                               uint64_t{1} << 63,
                               std::numeric_limits<uint64_t>::max()};
    for(uint64_t input: inputs)
    {
        Expected<Value> result = int_value_from_uint64(context.thread(), input);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(input <= static_cast<uint64_t>(value_smi_max),
                  result.value().is_smi());
        EXPECT_EQ(std::to_wstring(input), int_value_decimal(result.value()));
    }
}

TEST(BigInt, LengthValidationAcceptsBoxedPayloadsThroughInt64Max)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    for(Value value: {Value::False(), Value::True(), Value::from_smi(0),
                      Value::from_smi(value_smi_max)})
    {
        auto result = validate_length_result(value);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(value.is_bool() ? int64_t(value == Value::True())
                                  : value.get_smi(),
                  result.value());
    }
    Owned<Value> values(context.run_file(LR"(
class I(int):
    def __bool__(self):
        raise AssertionError
    def __index__(self):
        raise AssertionError
(I(0), I(1), I(2**58), I(2**63 - 1))
)"));
    ASSERT_TRUE(can_convert_to<Tuple>(values.value()));
    const int64_t expected[] = {0, 1, int64_t{1} << 58,
                                std::numeric_limits<int64_t>::max()};
    Tuple *tuple = values.value().get_ptr<Tuple>();
    ASSERT_EQ(std::size(expected), tuple->size());
    for(size_t idx = 0; idx < tuple->size(); ++idx)
    {
        ASSERT_TRUE(can_convert_to<BigInt>(tuple->item_unchecked(idx)));
        auto result = validate_length_result(tuple->item_unchecked(idx));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(expected[idx], result.value());
    }
}

TEST(BigInt, LengthValidationRejectsInvalidNegativeAndOverflowResults)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    struct InvalidLength
    {
        const wchar_t *source;
        const wchar_t *exception;
    };
    const InvalidLength cases[] = {
        {L"None\n", L"TypeError"},
        {L"1.5\n", L"TypeError"},
        {L"'1'\n", L"TypeError"},
        {L"class Index:\n    def __index__(self): return 1\nIndex()\n",
         L"TypeError"},
        {L"-1\n", L"ValueError"},
        {L"class I(int): pass\nI(-1)\n", L"ValueError"},
        {L"-(2**100)\n", L"ValueError"},
        {L"2**63\n", L"OverflowError"},
        {L"2**100\n", L"OverflowError"},
    };
    for(const InvalidLength &test_case: cases)
    {
        SCOPED_TRACE(test_case.source);
        Owned<Value> value(context.run_file(test_case.source));
        ASSERT_FALSE(value.value().is_exception_marker());
        auto result = validate_length_result(value.value());
        ASSERT_TRUE(result.has_exception());
        EXPECT_EQ(context.thread()->class_for_builtin_name(test_case.exception),
                  context.thread()
                      ->pending_exception_object()
                      .extract()
                      ->get_shape()
                      ->get_class());
        context.thread()->clear_pending_exception();
    }
    EXPECT_FALSE(context.thread()->has_pending_exception());
}
