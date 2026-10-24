#include "util/num_parse.h"

#include <cmath>
#include <cstddef>
#include <fast_float/fast_float.h>
#include <system_error>

namespace cl
{
    std::optional<double> try_parse_float(std::wstring_view text) noexcept
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
}  // namespace cl
