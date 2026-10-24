#include "builtin_types/num_parse.h"

#include "builtin_types/bigint.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fast_float/fast_float.h>
#include <iterator>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace cl
{
    namespace
    {
#include "builtin_types/num_parse_unicode.inc"

        // original is a borrowed, trimmed view. rewritten owns any changed
        // text; no stored view points into that buffer, so moves preserve its
        // validity.
        template <typename Char> struct NormalizedNumberText
        {
            std::basic_string_view<Char> original;
            std::basic_string<Char> rewritten;

            std::basic_string_view<Char> text() const
            {
                return rewritten.empty()
                           ? original
                           : std::basic_string_view<Char>(rewritten);
            }
        };
    }  // namespace

    template <typename Char>
    static std::optional<double>
    try_parse_float_direct(std::basic_string_view<Char> text) noexcept
    {
        if(text.empty())
        {
            return std::nullopt;
        }
        double value;
        const Char *end = text.data() + text.size();
        auto result = fast_float::from_chars(
            text.data(), end, value,
            fast_float::chars_format::general |
                fast_float::chars_format::allow_leading_plus);
        if(result.ptr != end || (result.ec != std::errc() &&
                                 result.ec != std::errc::result_out_of_range))
        {
            return std::nullopt;
        }
        // fast_float also accepts C-style nan(payload) spellings.
        if(std::isnan(value))
        {
            size_t sign_length = text.front() == '+' || text.front() == '-';
            if(text.size() != sign_length + 3)
            {
                return std::nullopt;
            }
        }
        return value;
    }

    template <typename Char> static bool is_numeric_whitespace(Char character)
    {
        uint32_t codepoint = static_cast<uint32_t>(character);
        if(codepoint < 0x80)
        {
            return codepoint == 0x20 ||
                   (codepoint >= 0x09 && codepoint <= 0x0d);
        }
        if constexpr(std::is_same_v<Char, char>)
        {
            return false;
        }
        return std::binary_search(std::begin(numeric_whitespace_codepoints),
                                  std::end(numeric_whitespace_codepoints),
                                  codepoint);
    }

    static int decimal_digit_value(uint32_t codepoint)
    {
        if(codepoint < 0x80)
        {
            return codepoint >= '0' && codepoint <= '9'
                       ? static_cast<int>(codepoint - '0')
                       : -1;
        }
        const uint32_t *upper =
            std::upper_bound(std::begin(decimal_zero_codepoints),
                             std::end(decimal_zero_codepoints), codepoint);
        if(upper == std::begin(decimal_zero_codepoints))
        {
            return -1;
        }
        uint32_t digit = codepoint - upper[-1];
        return digit < 10 ? static_cast<int>(digit) : -1;
    }

    template <typename Char>
    static std::optional<uint32_t>
    next_codepoint(std::basic_string_view<Char> text, size_t &position)
    {
        uint32_t codepoint = static_cast<uint32_t>(text[position++]);
        if constexpr(std::is_same_v<Char, char>)
        {
            if(codepoint >= 0x80)
            {
                return std::nullopt;
            }
            return codepoint;
        }
        if constexpr(sizeof(Char) == 2)
        {
            if(codepoint >= 0xd800 && codepoint <= 0xdbff)
            {
                if(position == text.size())
                {
                    return std::nullopt;
                }
                uint32_t low = static_cast<uint32_t>(text[position++]);
                if(low < 0xdc00 || low > 0xdfff)
                {
                    return std::nullopt;
                }
                codepoint =
                    0x10000 + ((codepoint - 0xd800) << 10) + low - 0xdc00;
            }
        }
        if((codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
        {
            return std::nullopt;
        }
        return codepoint;
    }

    template <typename Char>
    static std::optional<NormalizedNumberText<Char>>
    normalize_number_text(std::basic_string_view<Char> text)
    {
        while(!text.empty() && is_numeric_whitespace(text.front()))
        {
            text.remove_prefix(1);
        }
        while(!text.empty() && is_numeric_whitespace(text.back()))
        {
            text.remove_suffix(1);
        }

        // Allocate only when normalization changes characters. Trimming alone
        // can use the original storage.
        std::basic_string<Char> normalized;
        bool rewritten = false;
        bool previous_was_digit = false;
        bool underscore_pending = false;
        size_t position = 0;
        while(position < text.size())
        {
            size_t source_position = position;
            auto codepoint = next_codepoint(text, position);
            if(!codepoint)
            {
                return std::nullopt;
            }
            bool underscore = *codepoint == '_';
            int digit = decimal_digit_value(*codepoint);
            if(underscore)
            {
                if(!previous_was_digit)
                {
                    return std::nullopt;
                }
                previous_was_digit = false;
                underscore_pending = true;
            }
            else
            {
                if((underscore_pending && digit < 0) ||
                   (*codepoint >= 0x80 && digit < 0))
                {
                    return std::nullopt;
                }
                previous_was_digit = digit >= 0;
                underscore_pending = false;
            }
            if(!rewritten && (underscore || *codepoint >= 0x80))
            {
                normalized.reserve(text.size());
                normalized.append(text.substr(0, source_position));
                rewritten = true;
            }
            if(rewritten && !underscore)
            {
                normalized.push_back(digit >= 0
                                         ? static_cast<Char>('0' + digit)
                                         : static_cast<Char>(*codepoint));
            }
        }
        if(underscore_pending)
        {
            return std::nullopt;
        }
        return NormalizedNumberText<Char>{text, std::move(normalized)};
    }

    // Keep ordinary SMI text on the original allocation-free digit loop.
    // A miss defers validation, normalization, and wider values to the
    // fallback.
    static Value try_parse_smi_string(std::wstring_view text)
    {
        if(text.empty())
        {
            return Value::not_present();
        }
        size_t digit_begin = 0;
        bool negative = false;
        if(text.front() == L'+' || text.front() == L'-')
        {
            negative = text.front() == L'-';
            digit_begin = 1;
        }

        static constexpr uint64_t positive_limit =
            static_cast<uint64_t>(value_smi_max);
        static constexpr uint64_t negative_limit = positive_limit + 1;
        uint64_t limit = negative ? negative_limit : positive_limit;
        uint64_t parsed = 0;
        bool saw_digit = false;
        for(size_t idx = digit_begin; idx < text.size(); ++idx)
        {
            wchar_t ch = text[idx];
            if(ch < L'0' || ch > L'9')
            {
                return Value::not_present();
            }
            uint64_t digit = static_cast<uint64_t>(ch - L'0');
            if(parsed > (limit - digit) / 10)
            {
                return Value::not_present();
            }
            parsed = parsed * 10 + digit;
            saw_digit = true;
        }
        if(!saw_digit)
        {
            return Value::not_present();
        }
        int64_t result = static_cast<int64_t>(parsed);
        return Value::from_smi(negative ? -result : result);
    }

    enum class Int64ParseStatus
    {
        Parsed,
        NeedsBigInt,
        Invalid
    };

    struct Int64ParseResult
    {
        Int64ParseStatus status;
        int64_t value = 0;
    };

    static Int64ParseResult parse_int_string_direct(std::wstring_view text,
                                                    int base = 10)
    {
        if(text.empty())
        {
            return {Int64ParseStatus::Invalid};
        }
        int64_t value;
        const wchar_t *end = text.data() + text.size();
        auto result = fast_float::from_chars_advanced(
            text.data(), end, value,
            fast_float::parse_options_t<wchar_t>(
                fast_float::chars_format::allow_leading_plus, L'.', base));
        if(result.ptr != end)
        {
            return {Int64ParseStatus::Invalid};
        }
        if(result.ec == std::errc())
        {
            return {Int64ParseStatus::Parsed, value};
        }
        if(result.ec == std::errc::result_out_of_range)
        {
            return {Int64ParseStatus::NeedsBigInt};
        }
        return {Int64ParseStatus::Invalid};
    }

    static int ascii_int_digit_value(wchar_t ch)
    {
        if(ch >= L'0' && ch <= L'9')
        {
            return ch - L'0';
        }
        if(ch >= L'a' && ch <= L'z')
        {
            return ch - L'a' + 10;
        }
        if(ch >= L'A' && ch <= L'Z')
        {
            return ch - L'A' + 10;
        }
        return -1;
    }

    // The direct parser has proved that text is an optional ASCII sign followed
    // by valid digits in base, with a value outside int64 range.
    [[nodiscard]] static Expected<Value>
    build_bigint_from_text(ThreadState *thread, std::wstring_view text,
                           int base = 10)
    {
        assert(!text.empty());
        bool negative = text.front() == L'-';
        if(text.front() == L'+' || negative)
        {
            text.remove_prefix(1);
        }
        assert(!text.empty());

        size_t scratch_capacity = text.size() + 1;
        BigIntScratch scratch0(scratch_capacity);
        BigIntScratch scratch1(scratch_capacity);
        MutableBigIntView initial = scratch0.mutable_view();
        ConstBigIntView current{0, 0, initial.digits};
        bool use_first_scratch = false;
        for(wchar_t ch: text)
        {
            int digit = ascii_int_digit_value(ch);
            assert(digit >= 0 && digit < base);
            MutableBigIntView dest = use_first_scratch
                                         ? scratch0.mutable_view()
                                         : scratch1.mutable_view();
            bigint_abs_mul_add_u32(&dest, current, static_cast<uint32_t>(base),
                                   static_cast<uint32_t>(digit));
            current = dest.view();
            use_first_scratch = !use_first_scratch;
        }
        current = normalize_bigint_view(current);
        if(current.signum != 0)
        {
            current.signum = negative ? -1 : 1;
        }
        return finalize_bigint(thread, current);
    }

    [[nodiscard]] NOINLINE static Expected<Value>
    parse_normalized_int_string(ThreadState *thread, std::wstring_view text)
    {
        auto normalized = normalize_number_text(text);
        if(normalized)
        {
            std::wstring_view normalized_text = normalized->text();
            Int64ParseResult result = parse_int_string_direct(normalized_text);
            if(result.status == Int64ParseStatus::Parsed)
            {
                return bigint_from_int64(thread, result.value);
            }
            if(result.status == Int64ParseStatus::NeedsBigInt)
            {
                return build_bigint_from_text(thread, normalized_text);
            }
        }
        return Expected<Value>::raise_exception(L"ValueError",
                                                L"invalid literal for int()");
    }

    [[nodiscard]] NOINLINE static Expected<Value>
    parse_int_string_with_library(ThreadState *thread, std::wstring_view text)
    {
        Int64ParseResult result = parse_int_string_direct(text);
        if(result.status == Int64ParseStatus::Parsed)
        {
            return bigint_from_int64(thread, result.value);
        }
        if(result.status == Int64ParseStatus::NeedsBigInt)
        {
            return build_bigint_from_text(thread, text);
        }
        return parse_normalized_int_string(thread, text);
    }

    Expected<Value> parse_int_string_view(ThreadState *thread,
                                          std::wstring_view text)
    {
        Value smi = try_parse_smi_string(text);
        if(!smi.is_not_present())
        {
            return Expected<Value>::ok(smi);
        }
        return parse_int_string_with_library(thread, text);
    }

    // Explicit-base grammar is separate from float/decimal normalization:
    // letters can be digits, and a single underscore may follow a prefix.
    static std::optional<NormalizedNumberText<wchar_t>>
    normalize_based_int_text(std::wstring_view text, int &base)
    {
        while(!text.empty() &&
              is_numeric_whitespace(static_cast<uint32_t>(text.front())))
        {
            text.remove_prefix(1);
        }
        while(!text.empty() &&
              is_numeric_whitespace(static_cast<uint32_t>(text.back())))
        {
            text.remove_suffix(1);
        }
        size_t position = 0;
        if(!text.empty() && (text.front() == L'+' || text.front() == L'-'))
        {
            ++position;
        }
        size_t sign_length = position;
        if(position == text.size())
        {
            return std::nullopt;
        }
        size_t after_first = position;
        auto first = next_codepoint(text, after_first);
        if(!first)
        {
            return std::nullopt;
        }
        bool starts_with_zero = decimal_digit_value(*first) == 0;
        int prefix_base = 0;
        if(starts_with_zero && after_first < text.size())
        {
            switch(text[after_first])
            {
                case L'b':
                case L'B':
                    prefix_base = 2;
                    break;
                case L'o':
                case L'O':
                    prefix_base = 8;
                    break;
                case L'x':
                case L'X':
                    prefix_base = 16;
                    break;
                default:
                    break;
            }
        }
        bool has_prefix =
            prefix_base != 0 && (base == 0 || base == prefix_base);
        bool require_zero = base == 0 && !has_prefix && starts_with_zero;
        if(base == 0)
        {
            base = has_prefix ? prefix_base : 10;
        }
        std::wstring normalized;
        bool rewritten = has_prefix;
        if(has_prefix)
        {
            normalized.reserve(text.size());
            normalized.append(text.substr(0, sign_length));
            position = after_first + 1;
            if(position < text.size() && text[position] == L'_')
            {
                ++position;
            }
        }
        bool previous_was_digit = false;
        while(position < text.size())
        {
            size_t source_position = position;
            auto codepoint = next_codepoint(text, position);
            if(!codepoint)
            {
                return std::nullopt;
            }
            bool underscore = *codepoint == L'_';
            int digit =
                *codepoint < 0x80
                    ? ascii_int_digit_value(static_cast<wchar_t>(*codepoint))
                    : decimal_digit_value(*codepoint);
            if(underscore)
            {
                if(!previous_was_digit)
                {
                    return std::nullopt;
                }
                previous_was_digit = false;
            }
            else
            {
                if(digit < 0 || digit >= base || (require_zero && digit != 0))
                {
                    return std::nullopt;
                }
                previous_was_digit = true;
            }
            if(!rewritten && (underscore || *codepoint >= 0x80))
            {
                normalized.reserve(text.size());
                normalized.append(text.substr(0, source_position));
                rewritten = true;
            }
            if(rewritten && !underscore)
            {
                normalized.push_back(*codepoint < 0x80
                                         ? static_cast<wchar_t>(*codepoint)
                                         : static_cast<wchar_t>(L'0' + digit));
            }
        }
        if(!previous_was_digit)
        {
            return std::nullopt;
        }
        return NormalizedNumberText<wchar_t>{text, std::move(normalized)};
    }

    Expected<Value> parse_int_string_view(ThreadState *thread,
                                          std::wstring_view text, int base)
    {
        assert(base == 0 || (base >= 2 && base <= 36));
        if(base == 10)
        {
            return parse_int_string_view(thread, text);
        }
        if(base != 0)
        {
            Int64ParseResult result = parse_int_string_direct(text, base);
            if(result.status == Int64ParseStatus::Parsed)
            {
                return bigint_from_int64(thread, result.value);
            }
            if(result.status == Int64ParseStatus::NeedsBigInt)
            {
                return build_bigint_from_text(thread, text, base);
            }
        }
        auto normalized = normalize_based_int_text(text, base);
        if(normalized)
        {
            std::wstring_view digits = normalized->text();
            Int64ParseResult result = parse_int_string_direct(digits, base);
            if(result.status == Int64ParseStatus::Parsed)
            {
                return bigint_from_int64(thread, result.value);
            }
            if(result.status == Int64ParseStatus::NeedsBigInt)
            {
                return build_bigint_from_text(thread, digits, base);
            }
        }
        return Expected<Value>::raise_exception(L"ValueError",
                                                L"invalid literal for int()");
    }

    template <typename Char>
    static std::optional<double>
    try_parse_float_impl(std::basic_string_view<Char> text)
    {
        if(auto value = try_parse_float_direct(text))
        {
            return value;
        }
        auto normalized = normalize_number_text(text);
        return normalized ? try_parse_float_direct(normalized->text())
                          : std::nullopt;
    }

    std::optional<double> try_parse_float(std::wstring_view text)
    {
        return try_parse_float_impl(text);
    }

    std::optional<double> try_parse_float(std::string_view text)
    {
        return try_parse_float_impl(text);
    }
}  // namespace cl
