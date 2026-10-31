#ifndef CL_BINARY_STRIP_H
#define CL_BINARY_STRIP_H

#include "builtin_types/strip_scan.h"
#include <array>
#include <optional>
#include <string_view>

namespace cl
{
    // The caller validates stored binary payloads before borrowing. No VM or
    // native heap allocation, callbacks, safepoints, or mutation occur here.
    inline StripRange scan_binary_strip(std::string_view source,
                                        std::optional<std::string_view> chars,
                                        StripMode mode)
    {
        if(!chars.has_value())
        {
            return scan_strip(source, mode, [](char ch) {
                return ch == ' ' || (ch >= '\t' && ch <= '\r');
            });
        }
        std::array<bool, 256> removable{};
        for(unsigned char ch: *chars)
            removable[ch] = true;
        return scan_strip(source, mode, [&](char ch) {
            return removable[static_cast<unsigned char>(ch)];
        });
    }
}  // namespace cl

#endif  // CL_BINARY_STRIP_H
