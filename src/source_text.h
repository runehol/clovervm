#ifndef CL_SOURCE_TEXT_H
#define CL_SOURCE_TEXT_H

#include <optional>
#include <string>

namespace cl
{
    std::optional<std::wstring> decode_source_text(const std::string &bytes);
    std::optional<std::wstring>
    read_source_text_file(const std::wstring &filename);

}  // namespace cl

#endif  // CL_SOURCE_TEXT_H
