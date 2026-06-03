#ifndef CL_UNICODE_H
#define CL_UNICODE_H

#include "builtin_types/str.h"
#include <optional>
#include <string>
#include <string_view>

namespace cl::unicode
{
    struct Utf8WcharLayout
    {
        size_t code_unit_count = 0;
    };

    [[nodiscard]] std::optional<Utf8WcharLayout>
    validate_utf8_for_wchar(std::string_view bytes);

    [[nodiscard]] bool decode_utf8_into_wchar(std::string_view bytes,
                                              cl_wchar *out, size_t out_count);

    [[nodiscard]] std::optional<std::wstring>
    decode_utf8(std::string_view bytes);

    [[nodiscard]] std::optional<std::wstring>
    decode_utf8_c_string(const char *bytes);

    [[nodiscard]] std::string encode_utf8(std::wstring_view text);

    [[nodiscard]] bool is_ascii(std::string_view bytes);
    [[nodiscard]] bool is_ascii(std::wstring_view text);

}  // namespace cl::unicode

#endif  // CL_UNICODE_H
