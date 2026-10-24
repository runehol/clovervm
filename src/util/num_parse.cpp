#include "util/num_parse.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fast_float/fast_float.h>
#include <iterator>
#include <string>
#include <system_error>

namespace cl
{
    namespace
    {
#include "util/num_parse_unicode.inc"
    }

    static std::optional<double>
    try_parse_float_direct(std::wstring_view text) noexcept
    {
        if(text.empty())
        {
            return std::nullopt;
        }
        double value;
        const wchar_t *end = text.data() + text.size();
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
            size_t sign_length = text.front() == L'+' || text.front() == L'-';
            if(text.size() != sign_length + 3)
            {
                return std::nullopt;
            }
        }
        return value;
    }

    static bool is_numeric_whitespace(uint32_t codepoint)
    {
        if(codepoint < 0x80)
        {
            return codepoint == 0x20 ||
                   (codepoint >= 0x09 && codepoint <= 0x0d);
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

    static std::optional<uint32_t> next_codepoint(std::wstring_view text,
                                                  size_t &position)
    {
        uint32_t codepoint = static_cast<uint32_t>(text[position++]);
        if constexpr(sizeof(wchar_t) == 2)
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

    static std::optional<double>
    normalize_and_parse_float(std::wstring_view text)
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

        // Allocate only when normalization changes characters. Trimming alone
        // can use the original storage.
        std::wstring normalized;
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
                                         ? static_cast<wchar_t>('0' + digit)
                                         : static_cast<wchar_t>(*codepoint));
            }
        }
        if(underscore_pending)
        {
            return std::nullopt;
        }
        return try_parse_float_direct(rewritten ? std::wstring_view(normalized)
                                                : text);
    }

    std::optional<double> try_parse_float(std::wstring_view text)
    {
        if(auto value = try_parse_float_direct(text))
        {
            return value;
        }
        return normalize_and_parse_float(text);
    }
}  // namespace cl
