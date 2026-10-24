#include "util/num_parse.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <string_view>

namespace cl
{
    TEST(FloatParse, DecimalFormsAndExactRounding)
    {
        struct Sample
        {
            std::wstring_view text;
            double expected;
        };
        const Sample samples[] = {
            {L"0", 0.0},
            {L"-0", -0.0},
            {L"+1.25", 1.25},
            {L"-.5", -0.5},
            {L"5.", 5.0},
            {L"00012.5", 12.5},
            {L"1.25e+3", 1250.0},
            {L"1E-3", 0.001},
            {L"9007199254740993", 9007199254740992.0},
            {L"1.00000000000000011102230246251565404236316680908203125", 1.0},
            {L"1.00000000000000011102230246251565404236316680908203126",
             0x1.0000000000001p0},
            {L"1.7976931348623157e308", std::numeric_limits<double>::max()},
            {L"2.2250738585072014e-308", std::numeric_limits<double>::min()},
            {L"4.9406564584124654e-324",
             std::numeric_limits<double>::denorm_min()},
            {L"-4.9406564584124654e-324",
             -std::numeric_limits<double>::denorm_min()},
        };
        for(const Sample &sample: samples)
        {
            SCOPED_TRACE(std::wstring(sample.text));
            auto value = try_parse_float(sample.text);
            ASSERT_TRUE(value.has_value());
            EXPECT_EQ(std::bit_cast<uint64_t>(sample.expected),
                      std::bit_cast<uint64_t>(*value));
        }
    }

    TEST(FloatParse, RangeErrorsProduceInfinityOrSignedZero)
    {
        for(std::wstring_view text:
            {L"1e9999", L"-1e9999", L"+1e9999999999999999999999",
             L"1.7976931348623159e308"})
        {
            auto value = try_parse_float(text);
            ASSERT_TRUE(value.has_value());
            EXPECT_TRUE(std::isinf(*value));
            EXPECT_EQ(text.front() == L'-', std::signbit(*value));
        }
        for(std::wstring_view text:
            {L"1e-9999", L"-1e-9999", L"1e-9999999999999999999999",
             L"0e9999999999999999999999", L"-0e9999999999999999999999",
             L"2.4703282292062327e-324"})
        {
            auto value = try_parse_float(text);
            ASSERT_TRUE(value.has_value());
            EXPECT_EQ(0.0, *value);
            EXPECT_EQ(text.front() == L'-', std::signbit(*value));
        }
    }

    TEST(FloatParse, SignedCaseInsensitiveSpecialValues)
    {
        for(std::wstring_view text:
            {L"inf", L"+INF", L"-Inf", L"infinity", L"-INFINITY"})
        {
            auto value = try_parse_float(text);
            ASSERT_TRUE(value.has_value());
            EXPECT_TRUE(std::isinf(*value));
            EXPECT_EQ(text.front() == L'-', std::signbit(*value));
        }
        for(std::wstring_view text: {L"nan", L"+NaN", L"-NAN"})
        {
            auto value = try_parse_float(text);
            ASSERT_TRUE(value.has_value());
            EXPECT_TRUE(std::isnan(*value));
            EXPECT_EQ(text.front() == L'-', std::signbit(*value));
        }
    }

    TEST(FloatParse, RequiresCompleteUnnormalizedInput)
    {
        for(std::wstring_view text:
            {L"",          L"+",         L"-",
             L".",         L"1e",        L"1e+",
             L"1.2x",      L" 1.2",      L"1.2 ",
             L"1_000",     L"1,000",     L"1 000",
             L"0x1.8p+1",  L"nan()",     L"nan(payload)",
             L"-NaN(foo)", L"+nan(123)", L"infinite",
             L"\u0661.5",  L"\u00a01.5", L"\u22121.5"})
        {
            SCOPED_TRACE(std::wstring(text));
            EXPECT_FALSE(try_parse_float(text).has_value());
        }
        EXPECT_FALSE(try_parse_float(std::wstring_view{}).has_value());
        EXPECT_FALSE(
            try_parse_float(std::wstring_view(L"1.5\0junk", 8)).has_value());
        EXPECT_FALSE(
            try_parse_float(std::wstring_view(L"1.5\0", 4)).has_value());
        EXPECT_FALSE(try_parse_float(L"1e9999junk").has_value());
    }

    TEST(FloatParse, RespectsViewBoundsWithoutRequiringTerminator)
    {
        const wchar_t buffer[] = {L'1', L'.', L'2', L'5', L'x'};
        auto value = try_parse_float(std::wstring_view(buffer, 4));
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(1.25, *value);
    }
}  // namespace cl
