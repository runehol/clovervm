#ifndef CL_TOKENIZER_H
#define CL_TOKENIZER_H

#include "token.h"
#include <string_view>

namespace cl
{
    struct CompilationUnit;
    TokenVector tokenize(CompilationUnit &cu);

    std::wstring_view string_for_name_token(std::wstring_view source, uint32_t offset);
    std::wstring_view string_for_number_token(std::wstring_view source, uint32_t offset);

}

#endif //CL_TOKENIZER_H
