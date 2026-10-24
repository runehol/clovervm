#ifndef CL_NUM_PARSE_H
#define CL_NUM_PARSE_H

#include <optional>
#include <string_view>

namespace cl
{
    // Parse Python float text, including surrounding numeric whitespace,
    // digit separators, and Unicode decimal digits. Ordinary input is parsed
    // directly; normalization is attempted only after direct parsing fails.
    // Overflow and underflow produce infinity and signed zero respectively.
    [[nodiscard]] std::optional<double> try_parse_float(std::wstring_view text);
}  // namespace cl

#endif  // CL_NUM_PARSE_H
