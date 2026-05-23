#include "source_text.h"

#include <cerrno>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <istream>

namespace cl
{
    std::optional<std::wstring> decode_source_text(const std::string &bytes)
    {
        const char *src = bytes.c_str();
        std::mbstate_t state = std::mbstate_t();
        errno = 0;
        size_t size = std::mbsrtowcs(nullptr, &src, 0, &state);
        if(size == static_cast<size_t>(-1))
        {
            return std::nullopt;
        }

        std::wstring result(size, L'\0');
        src = bytes.c_str();
        state = std::mbstate_t();
        errno = 0;
        if(std::mbsrtowcs(result.data(), &src, result.size(), &state) ==
           static_cast<size_t>(-1))
        {
            return std::nullopt;
        }
        return result;
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
