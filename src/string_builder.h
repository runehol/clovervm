#ifndef CL_STRING_BUILDER_H
#define CL_STRING_BUILDER_H

#include "str.h"
#include "value.h"
#include <string>

namespace cl
{
    class StringBuilder
    {
    public:
        void append_char(cl_wchar ch);
        void append_c_str(const cl_wchar *text);
        void append_string(TValue<String> string);
        [[nodiscard]] Value append_repr(Value value);
        [[nodiscard]] Value append_str(Value value);
        Value finish();

    private:
        std::wstring buffer;
    };

}  // namespace cl

#endif  // CL_STRING_BUILDER_H
