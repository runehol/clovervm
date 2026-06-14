#include "builtin_types/bigint.h"

#include "builtin_types/int.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

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
        EXPECT_STREQ(message, exception.extract()->message.extract()->data);
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

    Expected<Value> value = bigint_from_int64(context.thread(), value_smi_min);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(Value::from_smi(value_smi_min), value.value());
}

TEST(BigInt, BigIntFromInt64CanProduceHeapBigInt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Expected<Value> value = bigint_from_int64(
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
    Expected<Value> value = bigint_from_int64(
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

TEST(BigInt, IntlikeValueToSmiAcceptsBoolAndRejectsNonIntegers)
{
    TValue<SMI> bool_smi = TValue<SMI>::from_smi(0);
    TValue<SMI> none_smi = TValue<SMI>::from_smi(7);
    Expected<IntToSmiStatus> bool_result =
        try_intlike_value_to_smi(Value::True(), &bool_smi);
    Expected<IntToSmiStatus> none_result =
        try_intlike_value_to_smi(Value::None(), &none_smi);

    ASSERT_TRUE(bool_result.has_value());
    EXPECT_EQ(IntToSmiStatus::Converted, bool_result.value());
    EXPECT_EQ(1, bool_smi.extract());
    ASSERT_TRUE(none_result.has_value());
    EXPECT_EQ(IntToSmiStatus::NotInt, none_result.value());
    EXPECT_EQ(7, none_smi.extract());
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
