#include "python_exception.h"
#include <utility>

namespace cl
{
    static std::string narrow_wstring(const std::wstring &message)
    {
        std::string result;
        result.reserve(message.size());
        for(wchar_t ch: message)
        {
            result.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch)
                                                   : '?');
        }
        return result;
    }

    PythonException::PythonException(std::wstring message)
        : wide_message(std::move(message)),
          narrow_message(narrow_wstring(wide_message))
    {
    }
}  // namespace cl
