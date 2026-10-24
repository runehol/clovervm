#include "builtin_types/num_parse.h"

#include "builtin_types/bigint.h"
#include "object_model/owned.h"
#include "test_helpers.h"

#include <algorithm>
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

    TEST(FloatParse, ByteTextMatchesWideASCIIGrammarAndRounding)
    {
        const std::string_view samples[] = {
            "0",
            "-0",
            "+1.25",
            "-.5",
            "5.",
            "00012.5",
            "9007199254740993",
            "1.00000000000000011102230246251565404236316680908203125",
            "1.00000000000000011102230246251565404236316680908203126",
            "1.7976931348623157e308",
            "2.2250738585072014e-308",
            "4.9406564584124654e-324",
            "-4.9406564584124654e-324",
            "1e9999",
            "-1e9999",
            "-1e-9999",
            "1e-9999999999999999999999",
            "INF",
            "-infinity",
            "nan",
            "+NaN",
            "-NAN",
            " \t\n\r\v\f+1_000.2_5e-1 \t",
            "1e1_0",
            " \t-NaN ",
            "",
            " ",
            "+",
            "-",
            ".",
            "1e",
            "1e+",
            "1.2x",
            "1,000",
            "1 000",
            "0x1.8p+1",
            "nan()",
            "nan(payload)",
            "-NaN(foo)",
            "+nan(123)",
            "infinite",
            "_1",
            "1_",
            "1__0",
            "1_.0",
            "1._0",
            "1e_2",
            "1e+_2",
            "na_n",
            "in_f",
            "1_ 2",
            "1 _2",
            "\x1c"
            "1.5",
            "1.5\x1f",
            " \tnan(foo) "};
        for(std::string_view text: samples)
        {
            SCOPED_TRACE(std::string(text));
            auto parsed = try_parse_float(text);
            std::wstring wide(text.begin(), text.end());
            auto expected = try_parse_float(std::wstring_view(wide));
            ASSERT_EQ(expected.has_value(), parsed.has_value());
            if(expected)
            {
                EXPECT_EQ(std::bit_cast<uint64_t>(*expected),
                          std::bit_cast<uint64_t>(*parsed));
            }
        }
    }

    TEST(FloatParse, ByteTextRejectsEveryNonASCIIByte)
    {
        for(unsigned int byte = 0x80; byte <= 0xff; ++byte)
        {
            SCOPED_TRACE(byte);
            char ch = static_cast<char>(byte);
            for(const std::string &text:
                {std::string(1, ch) + "1.5", "1.5" + std::string(1, ch),
                 "1_" + std::string(1, ch), " " + std::string(1, ch) + " "})
            {
                EXPECT_FALSE(
                    try_parse_float(std::string_view(text)).has_value());
            }
        }
    }

    TEST(FloatParse, ByteTextRespectsBoundsAndRejectsEmbeddedNulls)
    {
        const char buffer[] = {'1', '.', '2', '5', 'x'};
        EXPECT_EQ(1.25, try_parse_float(std::string_view(buffer, 4)));
        EXPECT_FALSE(try_parse_float(std::string_view(buffer, 5)).has_value());
        EXPECT_FALSE(try_parse_float(std::string_view{}).has_value());
        EXPECT_FALSE(
            try_parse_float(std::string_view("1.5\0junk", 8)).has_value());
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
        ASSERT_TRUE(has_native_layout<BigInt>(value.value()));
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

    TEST(IntParse, ExplicitBasesAcrossSupportedRange)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        const std::wstring_view samples[] = {
            L"111010110111100110100010101",
            L"22121022020212200",
            L"13112330310111",
            L"223101104124",
            L"20130035113",
            L"3026236221",
            L"726746425",
            L"277266780",
            L"123456789",
            L"63762a05",
            L"35418a99",
            L"1c767471",
            L"12579781",
            L"ac89bc9",
            L"75bcd15",
            L"51g2a21",
            L"3b60f89",
            L"2bg64ae",
            L"1ibc1j9",
            L"194gh7f",
            L"11l0805",
            L"j43jfb",
            L"fc2egl",
            L"cg15le",
            L"aa44a1",
            L"8g86ni",
            L"74nqb1",
            L"60fshj",
            L"52ce69",
            L"49l302",
            L"3lnj8l",
            L"353c3r",
            L"2od2i1",
            L"2c9g1t",
            L"21i3v9",
        };
        int base = 2;
        for(std::wstring_view text: samples)
        {
            SCOPED_TRACE(base);
            auto parsed = parse_int_string_view(context.thread(), text, base);
            ASSERT_FALSE(parsed.has_exception());
            EXPECT_EQ(Value::from_smi(123456789), parsed.value());
            std::string bytes(text.begin(), text.end());
            auto parsed_bytes = parse_int_string_view(
                context.thread(), std::string_view(bytes), base++);
            ASSERT_FALSE(parsed_bytes.has_exception());
            EXPECT_EQ(Value::from_smi(123456789), parsed_bytes.value());
        }
    }

    TEST(IntParse, ExplicitBasesNormalizeAndBuildBigInts)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        struct Sample
        {
            std::wstring_view text;
            int base;
            std::wstring_view expected;
        };
        const Sample samples[] = {
            {L"0b_101", 0, L"5"},
            {L"0O_777", 8, L"511"},
            {L"  -0X_Ff \t", 0, L"-255"},
            {L"0x_ff", 16, L"255"},
            {L"\u0660x_f", 0, L"15"},
            {L"0b\u0661_\u0660", 0, L"2"},
            {L"ff_ff", 16, L"65535"},
            {L"z_Z", 36, L"1295"},
            {L"1_\u0662", 16, L"18"},
            {L"00", 0, L"0"},
            {L"0_0", 0, L"0"},
            {L"-000", 0, L"0"},
            {L"0010", 10, L"10"},
            {L"0b10", 16, L"2832"},
            {L"0x10", 36, L"42804"},
            {L"+123", 0, L"123"},
            {L"-0x8000000000000000", 0, L"-9223372036854775808"},
            {L"0x8000000000000000", 16, L"9223372036854775808"},
            {L"0xffffffffffffffff", 0, L"18446744073709551615"},
            {L"0x10000000000000000", 16, L"18446744073709551616"},
            {L"\u00a0-\U0001d7d9_f\u2003", 16, L"-31"},
            {L"\u0663_\u0664", 5, L"19"},
            {L"-100000000000000000000000000000000000000000000000000000000000000"
             L"0000000000000000000000000000000000000000000000000000000000000000"
             L"0000000000000000000011000000111001",
             2, L"-1461501637330902918203684832716283019655932555321"},
            {L"2211120021211021012111220122110102020121122220221120222201021000"
             L"2111111201101120221012122101221000111",
             3, L"1461501637330902918203684832716283019655932555321"},
            {L"662313122254561664144230006644464633403520441042540615606", 7,
             L"1461501637330902918203684832716283019655932555321"},
            {L"-10000000000000000000000000000000000003039", 16,
             L"-1461501637330902918203684832716283019655932555321"},
            {L"-TWJ4YIDKW7A8PN4G709KZMFOAOL46RD", 36,
             L"-1461501637330902918203684832716283019655932555321"},
            {L"ff_ffffffff_ffffffff", 16, L"4722366482869645213695"},
            {L"0000", 0, L"0"},
            {L"12345", 10, L"12345"},
        };
        for(const Sample &sample: samples)
        {
            SCOPED_TRACE(std::wstring(sample.text));
            SCOPED_TRACE(sample.base);
            auto parsed = parse_int_string_view(context.thread(), sample.text,
                                                sample.base);
            ASSERT_FALSE(parsed.has_exception());
            Owned<Value> value(parsed.value());
            std::wstring decimal =
                value.value().is_smi()
                    ? std::to_wstring(value.value().get_smi())
                    : bigint_to_decimal_string(
                          value.value().get_ptr<BigInt>()->view());
            EXPECT_EQ(sample.expected, decimal);
            if(std::all_of(sample.text.begin(), sample.text.end(),
                           [](wchar_t ch) { return ch < 0x80; }))
            {
                std::string bytes(sample.text.begin(), sample.text.end());
                auto parsed_bytes = parse_int_string_view(
                    context.thread(), std::string_view(bytes), sample.base);
                ASSERT_FALSE(parsed_bytes.has_exception());
                Owned<Value> byte_value(parsed_bytes.value());
                std::wstring byte_decimal =
                    byte_value.value().is_smi()
                        ? std::to_wstring(byte_value.value().get_smi())
                        : bigint_to_decimal_string(
                              byte_value.value().get_ptr<BigInt>()->view());
                EXPECT_EQ(sample.expected, byte_decimal);
            }
        }
    }

    TEST(IntParse, ExplicitBasesRejectInvalidText)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        struct Sample
        {
            std::wstring_view text;
            int base;
        };
        const Sample samples[] = {
            {L"", 2},
            {L" ", 0},
            {L"+", 16},
            {L"0x", 0},
            {L"0x_", 16},
            {L"0x__1", 0},
            {L"0_x1", 0},
            {L"_f", 16},
            {L"f_", 16},
            {L"f__f", 16},
            {L"0b2", 0},
            {L"0o8", 0},
            {L"0xg", 16},
            {L"01", 0},
            {L"0_1", 0},
            {L"00010", 0},
            {L"0xff", 10},
            {L"0o7", 16},
            {L"f", 15},
            {L"z", 35},
            {L"1 2", 16},
            {L"1,000", 16},
            {L"1.0", 16},
            {L"nan", 10},
            {L"\03412", 16},
            {L"12\037", 16},
            {L"\u00b2", 16},
            {L"\uff21", 16},
            {L"\u22121", 16},
            {L"\u0660\u0661", 0},
            {L"0b\u0662", 0},
            {L"ffffffffffffffffffffffffffffffffffffffffg", 16},
            {L"ffffffffffffffffffffffffffffffffffffffff_", 16},
            {std::wstring_view(L"1\0", 2), 16},
        };
        for(const Sample &sample: samples)
        {
            SCOPED_TRACE(std::wstring(sample.text));
            SCOPED_TRACE(sample.base);
            auto parsed = parse_int_string_view(context.thread(), sample.text,
                                                sample.base);
            EXPECT_TRUE(parsed.has_exception());
            EXPECT_TRUE(context.thread()->has_pending_exception());
            context.thread()->clear_pending_exception();
            if(std::all_of(sample.text.begin(), sample.text.end(),
                           [](wchar_t ch) { return ch < 0x80; }))
            {
                std::string bytes(sample.text.begin(), sample.text.end());
                auto parsed_bytes = parse_int_string_view(
                    context.thread(), std::string_view(bytes), sample.base);
                EXPECT_TRUE(parsed_bytes.has_exception());
                EXPECT_TRUE(context.thread()->has_pending_exception());
                context.thread()->clear_pending_exception();
            }
        }
    }

    TEST(IntParse, ByteDecimalTextPreservesSMIAndBigIntBoundaries)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        const std::string_view samples[] = {
            "0",
            "-0",
            "+12345",
            " \t\n\r\v\f+1_234_567 ",
            "288230376151711743",
            "288230376151711744",
            "-288230376151711744",
            "-288230376151711745",
            "9223372036854775807",
            "-9223372036854775808",
            "9223372036854775808",
            "-9223372036854775809",
            "18446744073709551615",
            "18446744073709551616",
            " \t-123_456_789_012_345_678_901_234_567_890 \n",
        };
        for(std::string_view text: samples)
        {
            SCOPED_TRACE(std::string(text));
            std::wstring wide(text.begin(), text.end());
            auto expected = parse_int_string_view(context.thread(),
                                                  std::wstring_view(wide));
            ASSERT_FALSE(expected.has_exception());
            Owned<Value> expected_value(expected.value());
            auto parsed = parse_int_string_view(context.thread(), text);
            ASSERT_FALSE(parsed.has_exception());
            Owned<Value> value(parsed.value());
            EXPECT_EQ(expected_value.value().is_smi(), value.value().is_smi());
            if(value.value().is_smi())
            {
                EXPECT_EQ(expected_value.value(), value.value());
            }
            else
            {
                EXPECT_EQ(bigint_to_decimal_string(
                              expected_value.value().get_ptr<BigInt>()->view()),
                          bigint_to_decimal_string(
                              value.value().get_ptr<BigInt>()->view()));
            }
        }
        const char buffer[] = {'1', '2', '3', '4', '5', 'x'};
        auto bounded = parse_int_string_view(context.thread(),
                                             std::string_view(buffer, 5));
        ASSERT_FALSE(bounded.has_exception());
        EXPECT_EQ(Value::from_smi(12345), bounded.value());
    }

    TEST(IntParse, ByteTextRejectsNonASCIIAndInvalidDecimalText)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        for(unsigned int byte = 0x80; byte <= 0xff; ++byte)
        {
            SCOPED_TRACE(byte);
            char ch = static_cast<char>(byte);
            for(const std::string &text:
                {std::string(1, ch) + "12", "12" + std::string(1, ch),
                 "1_" + std::string(1, ch), "0" + std::string(1, ch) + "f"})
            {
                auto parsed = parse_int_string_view(context.thread(),
                                                    std::string_view(text));
                EXPECT_TRUE(parsed.has_exception());
                context.thread()->clear_pending_exception();
                for(int base: {0, 16, 36})
                {
                    auto based = parse_int_string_view(
                        context.thread(), std::string_view(text), base);
                    EXPECT_TRUE(based.has_exception());
                    context.thread()->clear_pending_exception();
                }
            }
        }
        const std::string_view invalid[] = {std::string_view{},
                                            std::string_view("1\0", 2),
                                            "+",
                                            "_1",
                                            "1_",
                                            "1__0",
                                            "1.0",
                                            "1e2",
                                            "0xff",
                                            "1 2",
                                            "\x1c"
                                            "12",
                                            "12\x1f"};
        for(std::string_view text: invalid)
        {
            SCOPED_TRACE(std::string(text));
            auto parsed = parse_int_string_view(context.thread(), text);
            EXPECT_TRUE(parsed.has_exception());
            EXPECT_TRUE(context.thread()->has_pending_exception());
            context.thread()->clear_pending_exception();
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
