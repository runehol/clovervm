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
    void expect_smi_bigint_view(int64_t value, signum_t expected_signum,
                                size_t expected_n_digits,
                                digit_t expected_digit0,
                                digit_t expected_digit1)
    {
        SmiBigInt smi_bigint(value);
        ConstBigIntView view = smi_bigint.view();

        EXPECT_EQ(expected_signum, view.signum);
        EXPECT_EQ(expected_n_digits, view.n_digits);
        EXPECT_EQ(expected_digit0, view.digits[0]);
        EXPECT_EQ(expected_digit1, view.digits[1]);
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

TEST(BigInt, SmiBigIntZeroViewIsCanonical)
{
    expect_smi_bigint_view(0, 0, 0, 0, 0);
}

TEST(BigInt, SmiBigIntPositiveViewIsLittleEndian)
{
    expect_smi_bigint_view(1, 1, 1, 1, 0);
    expect_smi_bigint_view(int64_t{1} << kDigitBits, 1, 2, 0, 1);
}

TEST(BigInt, SmiBigIntNegativeViewUsesMagnitudeDigits)
{
    expect_smi_bigint_view(-1, -1, 1, 1, 0);
    expect_smi_bigint_view(-(int64_t{1} << kDigitBits), -1, 2, 0, 1);
}

TEST(BigInt, SmiBigIntBoundaryViewsFitInTwoDigits)
{
    double_digit_t max_magnitude = static_cast<double_digit_t>(value_smi_max);
    expect_smi_bigint_view(value_smi_max, 1, 2,
                           static_cast<digit_t>(max_magnitude),
                           static_cast<digit_t>(max_magnitude >> kDigitBits));

    double_digit_t min_magnitude = double_digit_t(-(value_smi_min + 1)) + 1;
    expect_smi_bigint_view(value_smi_min, -1, 2,
                           static_cast<digit_t>(min_magnitude),
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

TEST(BigInt, AbsAddPropagatesCarryAndNormalizesSign)
{
    digit_t left[] = {0xffffffffu, 0xffffffffu};
    digit_t right[] = {1};
    BigIntScratch scratch(3);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_abs_add(&dest, ConstBigIntView{2, 1, left},
                   ConstBigIntView{1, 1, right});

    EXPECT_EQ(3u, dest.n_digits);
    EXPECT_EQ(1, dest.signum);
    EXPECT_EQ(0u, dest.digits[0]);
    EXPECT_EQ(0u, dest.digits[1]);
    EXPECT_EQ(1u, dest.digits[2]);
}

TEST(BigInt, AbsSubBorrowsAndTrimsHighZeroDigits)
{
    digit_t left[] = {0, 1};
    digit_t right[] = {1};
    BigIntScratch scratch(2);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_abs_sub(&dest, ConstBigIntView{2, 1, left},
                   ConstBigIntView{1, 1, right});

    EXPECT_EQ(1u, dest.n_digits);
    EXPECT_EQ(1, dest.signum);
    EXPECT_EQ(0xffffffffu, dest.digits[0]);
}

TEST(BigInt, AddCombinesSignedMagnitudes)
{
    digit_t large[] = {5, 1};
    digit_t small[] = {7};
    BigIntScratch scratch(3);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_add(&dest, ConstBigIntView{2, 1, large},
               ConstBigIntView{1, -1, small});

    EXPECT_EQ(1u, dest.n_digits);
    EXPECT_EQ(1, dest.signum);
    EXPECT_EQ(0xfffffffeu, dest.digits[0]);

    digit_t normalized_digits[] = {dest.digits[0]};
    MutableBigIntView negative_dest = scratch.mutable_view();
    bigint_add(&negative_dest, ConstBigIntView{1, 1, small},
               ConstBigIntView{2, -1, large});

    EXPECT_EQ(1u, negative_dest.n_digits);
    EXPECT_EQ(-1, negative_dest.signum);
    EXPECT_EQ(normalized_digits[0], negative_dest.digits[0]);
}

TEST(BigInt, AddCanonicalizesCancellationToZero)
{
    digit_t left[] = {3, 2};
    digit_t right[] = {3, 2};
    BigIntScratch scratch(2);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_add(&dest, ConstBigIntView{2, 1, left},
               ConstBigIntView{2, -1, right});

    EXPECT_EQ(0u, dest.n_digits);
    EXPECT_EQ(0, dest.signum);
}

TEST(BigInt, SubUsesSignedRightOperand)
{
    digit_t left[] = {1};
    digit_t right[] = {2};
    BigIntScratch scratch(2);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_sub(&dest, ConstBigIntView{1, 1, left},
               ConstBigIntView{1, 1, right});

    EXPECT_EQ(1u, dest.n_digits);
    EXPECT_EQ(-1, dest.signum);
    EXPECT_EQ(1u, dest.digits[0]);
}

TEST(BigInt, MulCanonicalizesZero)
{
    digit_t zero[] = {0};
    digit_t value[] = {7, 1};
    BigIntScratch scratch(3);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_mul(&dest, ConstBigIntView{0, 0, zero},
               ConstBigIntView{2, -1, value});

    EXPECT_EQ(0u, dest.n_digits);
    EXPECT_EQ(0, dest.signum);
}

TEST(BigInt, MulTrimsUnusedHighDigit)
{
    digit_t left[] = {1};
    digit_t right[] = {1};
    BigIntScratch scratch(2);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_mul(&dest, ConstBigIntView{1, -1, left},
               ConstBigIntView{1, -1, right});

    EXPECT_EQ(1u, dest.n_digits);
    EXPECT_EQ(1, dest.signum);
    EXPECT_EQ(1u, dest.digits[0]);
}

TEST(BigInt, MulPropagatesSingleDigitCarry)
{
    digit_t left[] = {0xffffffffu};
    digit_t right[] = {0xffffffffu};
    BigIntScratch scratch(2);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_mul(&dest, ConstBigIntView{1, -1, left},
               ConstBigIntView{1, 1, right});

    EXPECT_EQ(2u, dest.n_digits);
    EXPECT_EQ(-1, dest.signum);
    EXPECT_EQ(1u, dest.digits[0]);
    EXPECT_EQ(0xfffffffeu, dest.digits[1]);
}

TEST(BigInt, MulAccumulatesMultiDigitProducts)
{
    digit_t left[] = {0xffffffffu, 1};
    digit_t right[] = {0xffffffffu, 1};
    BigIntScratch scratch(4);
    MutableBigIntView dest = scratch.mutable_view();

    bigint_mul(&dest, ConstBigIntView{2, 1, left},
               ConstBigIntView{2, -1, right});

    EXPECT_EQ(3u, dest.n_digits);
    EXPECT_EQ(-1, dest.signum);
    EXPECT_EQ(1u, dest.digits[0]);
    EXPECT_EQ(0xfffffffcu, dest.digits[1]);
    EXPECT_EQ(3u, dest.digits[2]);
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
