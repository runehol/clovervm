#ifndef CL_COMPILATION_UNIT_H
#define CL_COMPILATION_UNIT_H

#include <cwchar>
#include <string>
#include "token.h"

namespace cl
{

    struct CompilationUnit
    {
        std::wstring file_name;
        std::wstring source_code;

        TokenVector tokens;

    };

}

#endif //CL_COMPILATION_UNIT_H
