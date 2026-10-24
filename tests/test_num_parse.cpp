#include "builtin_types/num_parse.h"

#include "builtin_types/bigint.h"
#include "object_model/owned.h"
#include "test_helpers.h"

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

    TEST(FloatParse, RejectsInvalidPythonFloatText)
    {
        for(std::wstring_view text: {L"",
                                     L"+",
                                     L"-",
                                     L".",
                                     L"1e",
                                     L"1e+",
                                     L"1.2x",
                                     L"1,000",
                                     L"1 000",
                                     L"0x1.8p+1",
                                     L"nan()",
                                     L"nan(payload)",
                                     L"-NaN(foo)",
                                     L"+nan(123)",
                                     L"infinite",
                                     L"\u22121.5",
                                     L" \t\n",
                                     L"_1",
                                     L"1_",
                                     L"1__0",
                                     L"1_.0",
                                     L"1._0",
                                     L"1e_2",
                                     L"1e+_2",
                                     L"na_n",
                                     L"in_f",
                                     L"1_ 2",
                                     L"1 _2",
                                     L"1\u20032",
                                     L"\x1c"
                                     L"1.5",
                                     L"1.5\x1f",
                                     L"\u00b2.5",
                                     L"1\u066b5",
                                     L"\uff0b1.5",
                                     L" \tnan(foo) ",
                                     L"\u0661_",
                                     L"_\u0661",
                                     L"\u0661_\u2003\u0662"})
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

    TEST(FloatParse, NormalizesPythonNumericText)
    {
        struct Sample
        {
            std::wstring_view text;
            double expected;
        };
        const Sample samples[] = {
            {L"  +1_000.2_5e-1 \t\r\n", 100.025},
            {L"1e1_0", 1e10},
            {L"\u0661\u0662.\u0665", 12.5},
            {L"1_\u0662", 12.0},
            {L"\uff11.\uff12\uff15", 1.25},
            {L"\U0001d7d9.5", 1.5},
            {L"\U0001d7d9_\U0001d7da.5", 12.5},
            {L"\U00011f51.5", 1.5},
            {L"\u00a0-0\u2003", -0.0},
            {L"\u0085+1.5\u3000", 1.5},
            {L" -1e-9999 ", -0.0},
        };
        for(const Sample &sample: samples)
        {
            SCOPED_TRACE(std::wstring(sample.text));
            auto value = try_parse_float(sample.text);
            ASSERT_TRUE(value.has_value());
            EXPECT_EQ(std::bit_cast<uint64_t>(sample.expected),
                      std::bit_cast<uint64_t>(*value));
        }
        auto infinity = try_parse_float(L" \t+InFiNiTy\u2003");
        ASSERT_TRUE(infinity.has_value());
        EXPECT_TRUE(std::isinf(*infinity));
        auto nan = try_parse_float(L"\u00a0-NaN ");
        ASSERT_TRUE(nan.has_value());
        EXPECT_TRUE(std::isnan(*nan));
        EXPECT_TRUE(std::signbit(*nan));
    }

    TEST(FloatParse, RejectsInvalidUnicodeCodepoints)
    {
        for(uint32_t codepoint: {0xd800u, 0xdc00u})
        {
            std::wstring text(1, static_cast<wchar_t>(codepoint));
            text += L"1.5";
            EXPECT_FALSE(try_parse_float(text).has_value());
        }
        if constexpr(sizeof(wchar_t) == 4)
        {
            std::wstring text(1, static_cast<wchar_t>(0x110000));
            EXPECT_FALSE(try_parse_float(text).has_value());
        }
    }

    TEST(IntParse, NormalizedResultsSurviveInputDestruction)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Owned<Value> value(Value::None());
        {
            std::wstring input = L"12_345_678_901_234_567_890_123_456_789";
            auto parsed = parse_int_string_view(context.thread(), input);
            ASSERT_FALSE(parsed.has_exception());
            value = parsed.value();
        }
        ASSERT_TRUE(can_convert_to<BigInt>(value.value()));
        EXPECT_EQ(
            L"12345678901234567890123456789",
            bigint_to_decimal_string(value.value().get_ptr<BigInt>()->view()));
    }

    TEST(IntParse, ReturnsSMIFromBoundedAndNormalizedViews)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        const wchar_t buffer[] = {L'1', L'2', L'3', L'4', L'5', L'x'};
        for(std::wstring_view input: {std::wstring_view(buffer, 5),
                                      std::wstring_view(L" \t12_345\u2003")})
        {
            auto parsed = parse_int_string_view(context.thread(), input);
            ASSERT_FALSE(parsed.has_exception());
            ASSERT_TRUE(parsed.value().is_smi());
            EXPECT_EQ(Value::from_smi(12345), parsed.value());
        }
    }

    TEST(FloatParse, RespectsViewBoundsWithoutRequiringTerminator)
    {
        const wchar_t buffer[] = {L'1', L'.', L'2', L'5', L'x'};
        auto value = try_parse_float(std::wstring_view(buffer, 4));
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(1.25, *value);
    }
}  // namespace cl
