#ifndef CL_COMPILATION_UNIT_H
#define CL_COMPILATION_UNIT_H

#include <cwchar>
#include <string>
#include "token.h"

namespace cl
{

    struct CompilationUnit
    {
        explicit CompilationUnit(std::wstring _source_code)
            : file_name(L"<stdin>"),
              source_code(std::move(_source_code))
        {}

        CompilationUnit(std::wstring _file_name, std::wstring _source_code)
            : file_name(std::move(_file_name)),
              source_code(std::move(_source_code))
        {}

        std::wstring file_name;
        std::wstring source_code;

    };

}

#endif //CL_COMPILATION_UNIT_H
