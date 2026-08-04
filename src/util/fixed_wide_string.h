#ifndef CL_UTIL_FIXED_WIDE_STRING_H
#define CL_UTIL_FIXED_WIDE_STRING_H

#include <cstddef>

namespace cl
{
    template <size_t Size> struct FixedWideString
    {
        consteval FixedWideString(const wchar_t (&source)[Size])
        {
            for(size_t index = 0; index < Size; ++index)
            {
                value[index] = source[index];
            }
        }

        constexpr const wchar_t *c_str() const { return value; }
        constexpr size_t size() const { return Size - 1; }

        wchar_t value[Size];
    };

}  // namespace cl

#endif  // CL_UTIL_FIXED_WIDE_STRING_H
