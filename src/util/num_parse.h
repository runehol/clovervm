#ifndef CL_NUM_PARSE_H
#define CL_NUM_PARSE_H

#include <optional>
#include <string_view>

namespace cl
{
    // Parse the entire buffer without allocation or normalization. Accept
    // signed decimal numbers, inf/infinity, and nan, but not NaN payloads.
    // Overflow and underflow produce infinity and signed zero respectively.
    [[nodiscard]] std::optional<double>
    try_parse_float(std::wstring_view text) noexcept;
}  // namespace cl

#endif  // CL_NUM_PARSE_H
