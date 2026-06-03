#include "compiler/source_text.h"

#include "builtin_types/unicode.h"

#include <filesystem>
#include <fstream>
#include <istream>

namespace cl
{
    std::optional<std::wstring> decode_source_text(const std::string &bytes)
    {
        return unicode::decode_utf8(bytes);
    }

    std::optional<std::wstring>
    read_source_text_file(const std::wstring &filename)
    {
        std::ifstream stream(std::filesystem::path(filename), std::ios::binary);
        if(!stream)
        {
            return std::nullopt;
        }

        std::string bytes((std::istreambuf_iterator<char>(stream)),
                          std::istreambuf_iterator<char>());
        return decode_source_text(bytes);
    }

}  // namespace cl
