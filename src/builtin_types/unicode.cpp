#include "builtin_types/unicode.h"

#include <cassert>
#include <cstdint>

namespace cl::unicode
{
    namespace
    {
        bool is_continuation_byte(unsigned char byte)
        {
            return (byte & 0xc0) == 0x80;
        }

        bool append_wchar_units(uint32_t codepoint, cl_wchar *out,
                                size_t out_count, size_t &out_idx)
        {
            if constexpr(sizeof(cl_wchar) == 2)
            {
                if(codepoint > 0xffff)
                {
                    if(out_idx + 2 > out_count)
                    {
                        return false;
                    }
                    codepoint -= 0x10000;
                    out[out_idx++] =
                        static_cast<cl_wchar>(0xd800 + (codepoint >> 10));
                    out[out_idx++] =
                        static_cast<cl_wchar>(0xdc00 + (codepoint & 0x3ff));
                    return true;
                }
            }

            if(out_idx + 1 > out_count)
            {
                return false;
            }
            out[out_idx++] = static_cast<cl_wchar>(codepoint);
            return true;
        }

        size_t wchar_unit_count_for_codepoint(uint32_t codepoint)
        {
            if constexpr(sizeof(cl_wchar) == 2)
            {
                return codepoint > 0xffff ? 2 : 1;
            }
            return 1;
        }

        bool scan_utf8(std::string_view bytes, size_t *out_count, cl_wchar *out,
                       size_t out_capacity)
        {
            size_t count = 0;
            size_t out_idx = 0;
            const unsigned char *src =
                reinterpret_cast<const unsigned char *>(bytes.data());
            const unsigned char *end = src + bytes.size();
            while(src < end)
            {
                uint32_t codepoint = 0;
                size_t continuation_count = 0;
                unsigned char first = *src++;
                if(first < 0x80)
                {
                    codepoint = first;
                }
                else if((first & 0xe0) == 0xc0)
                {
                    codepoint = first & 0x1f;
                    continuation_count = 1;
                }
                else if((first & 0xf0) == 0xe0)
                {
                    codepoint = first & 0x0f;
                    continuation_count = 2;
                }
                else if((first & 0xf8) == 0xf0)
                {
                    codepoint = first & 0x07;
                    continuation_count = 3;
                }
                else
                {
                    return false;
                }

                if(static_cast<size_t>(end - src) < continuation_count)
                {
                    return false;
                }
                for(size_t idx = 0; idx < continuation_count; ++idx)
                {
                    unsigned char continuation = *src++;
                    if(!is_continuation_byte(continuation))
                    {
                        return false;
                    }
                    codepoint = (codepoint << 6) | (continuation & 0x3f);
                }

                if((continuation_count == 1 && codepoint < 0x80) ||
                   (continuation_count == 2 && codepoint < 0x800) ||
                   (continuation_count == 3 && codepoint < 0x10000) ||
                   codepoint > 0x10ffff ||
                   (codepoint >= 0xd800 && codepoint <= 0xdfff))
                {
                    return false;
                }

                count += wchar_unit_count_for_codepoint(codepoint);
                if(out != nullptr &&
                   !append_wchar_units(codepoint, out, out_capacity, out_idx))
                {
                    return false;
                }
            }

            if(out != nullptr && out_idx != out_capacity)
            {
                return false;
            }
            *out_count = count;
            return true;
        }

        void append_utf8_codepoint(std::string &result, uint32_t codepoint)
        {
            if(codepoint <= 0x7f)
            {
                result.push_back(static_cast<char>(codepoint));
            }
            else if(codepoint <= 0x7ff)
            {
                result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
                result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            else if(codepoint <= 0xffff)
            {
                result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
                result.push_back(
                    static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            else
            {
                result.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
                result.push_back(
                    static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
                result.push_back(
                    static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
        }
    }  // namespace

    std::optional<Utf8WcharLayout>
    validate_utf8_for_wchar(std::string_view bytes)
    {
        size_t count = 0;
        if(!scan_utf8(bytes, &count, nullptr, 0))
        {
            return std::nullopt;
        }
        return Utf8WcharLayout{count};
    }

    bool decode_utf8_into_wchar(std::string_view bytes, cl_wchar *out,
                                size_t out_count)
    {
        size_t count = 0;
        return scan_utf8(bytes, &count, out, out_count) && count == out_count;
    }

    std::optional<std::wstring> decode_utf8(std::string_view bytes)
    {
        std::optional<Utf8WcharLayout> layout = validate_utf8_for_wchar(bytes);
        if(!layout.has_value())
        {
            return std::nullopt;
        }

        std::wstring result(layout->code_unit_count, L'\0');
        if(!decode_utf8_into_wchar(bytes, result.data(), result.size()))
        {
            return std::nullopt;
        }
        return result;
    }

    std::optional<std::wstring> decode_utf8_c_string(const char *bytes)
    {
        if(bytes == nullptr)
        {
            return std::nullopt;
        }
        return decode_utf8(std::string_view(bytes));
    }

    std::string encode_utf8(std::wstring_view text)
    {
        std::string result;
        result.reserve(text.size());
        for(size_t idx = 0; idx < text.size(); ++idx)
        {
            uint32_t codepoint = static_cast<uint32_t>(text[idx]);
            if constexpr(sizeof(cl_wchar) == 2)
            {
                if(codepoint >= 0xd800 && codepoint <= 0xdbff &&
                   idx + 1 < text.size())
                {
                    uint32_t low = static_cast<uint32_t>(text[idx + 1]);
                    if(low >= 0xdc00 && low <= 0xdfff)
                    {
                        codepoint = 0x10000 + (((codepoint - 0xd800) << 10) |
                                               (low - 0xdc00));
                        ++idx;
                    }
                }
            }
            assert(codepoint <= 0x10ffff);
            append_utf8_codepoint(result, codepoint);
        }
        return result;
    }

    bool is_ascii(std::string_view bytes)
    {
        for(unsigned char byte: bytes)
        {
            if(byte > 0x7f)
            {
                return false;
            }
        }
        return true;
    }

    bool is_ascii(std::wstring_view text)
    {
        for(cl_wchar ch: text)
        {
            if(ch > 0x7f)
            {
                return false;
            }
        }
        return true;
    }

}  // namespace cl::unicode
