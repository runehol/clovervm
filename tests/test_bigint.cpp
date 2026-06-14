#include "builtin_types/bigint.h"

#include <gtest/gtest.h>

using namespace cl;

namespace
{
    void expect_smi_bigint_view(int64_t value, signum_t expected_signum,
                                uint32_t expected_n_digits,
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
}  // namespace

TEST(BigInt, SmiBigIntZeroViewIsCanonical)
{
    expect_smi_bigint_view(0, 0, 0, 0, 0);
}

TEST(BigInt, SmiBigIntPositiveViewIsLittleEndian)
{
    expect_smi_bigint_view(1, 1, 1, 1, 0);
    expect_smi_bigint_view(int64_t{1} << 32, 1, 2, 0, 1);
}

TEST(BigInt, SmiBigIntNegativeViewUsesMagnitudeDigits)
{
    expect_smi_bigint_view(-1, -1, 1, 1, 0);
    expect_smi_bigint_view(-(int64_t{1} << 32), -1, 2, 0, 1);
}

TEST(BigInt, SmiBigIntBoundaryViewsFitInTwoDigits)
{
    uint64_t max_magnitude = static_cast<uint64_t>(value_smi_max);
    expect_smi_bigint_view(value_smi_max, 1, 2,
                           static_cast<digit_t>(max_magnitude),
                           static_cast<digit_t>(max_magnitude >> 32));

    uint64_t min_magnitude = uint64_t(-(value_smi_min + 1)) + 1;
    expect_smi_bigint_view(value_smi_min, -1, 2,
                           static_cast<digit_t>(min_magnitude),
                           static_cast<digit_t>(min_magnitude >> 32));
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
