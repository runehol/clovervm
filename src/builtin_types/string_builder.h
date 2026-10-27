#ifndef CL_STRING_BUILDER_H
#define CL_STRING_BUILDER_H

#include "builtin_types/str.h"
#include "object_model/value.h"
#include <cstdint>
#include <string>

namespace cl
{
    class StringBuilder
    {
    public:
        void append_char(cl_wchar ch);
        void append_c_str(const cl_wchar *text);
        void append_string(TValue<String> string);
        // Append exactly width lowercase hex digits, zero-padded on the left.
        void append_hex(uint32_t value, unsigned width);
        [[nodiscard]] Value append_repr(Value value);
        [[nodiscard]] Value append_str(Value value);
        Value finish();

    private:
        std::wstring buffer;
    };

}  // namespace cl

#endif  // CL_STRING_BUILDER_H
