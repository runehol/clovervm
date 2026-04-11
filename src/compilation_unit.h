#ifndef CL_COMPILATION_UNIT_H
#define CL_COMPILATION_UNIT_H

#include "token.h"
#include <algorithm>
#include <cwchar>
#include <string>
#include <utility>

namespace cl
{

    struct CompilationUnit
    {
        static constexpr uint32_t tab_size = 8;

        explicit CompilationUnit(std::wstring _source_code)
            : file_name(L"<stdin>"), source_code(std::move(_source_code))
        {
        }

        CompilationUnit(std::wstring _file_name, std::wstring _source_code)
            : file_name(std::move(_file_name)),
              source_code(std::move(_source_code))
        {
        }

        std::wstring file_name;
        std::wstring source_code;

        std::wstring_view get_source_view() const { return source_code; }

        std::pair<uint32_t, uint32_t> get_line_column(uint32_t offset) const
        {
            uint32_t line = 1;
            uint32_t column = 1;
            uint32_t capped_offset =
                std::min<uint32_t>(offset, source_code.size());
            for(uint32_t i = 0; i < capped_offset; ++i)
            {
                wchar_t c = source_code[i];
                if(c == L'\r')
                {
                    ++line;
                    column = 1;
                    if(i + 1 < capped_offset && source_code[i + 1] == L'\n')
                    {
                        ++i;
                    }
                }
                else if(c == L'\n')
                {
                    ++line;
                    column = 1;
                }
                else if(c == L'\t')
                {
                    column = ((column - 1) / tab_size + 1) * tab_size + 1;
                }
                else
                {
                    ++column;
                }
            }
            return {line, column};
        }

        std::wstring_view get_line_view(uint32_t offset) const
        {
            uint32_t capped_offset =
                std::min<uint32_t>(offset, source_code.size());
            uint32_t line_start = capped_offset;
            while(line_start > 0 && source_code[line_start - 1] != L'\n' &&
                  source_code[line_start - 1] != L'\r')
            {
                --line_start;
            }

            uint32_t line_end = capped_offset;
            while(line_end < source_code.size() &&
                  source_code[line_end] != L'\n' &&
                  source_code[line_end] != L'\r')
            {
                ++line_end;
            }

            return std::wstring_view(source_code)
                .substr(line_start, line_end - line_start);
        }
    };

}  // namespace cl

#endif  // CL_COMPILATION_UNIT_H
