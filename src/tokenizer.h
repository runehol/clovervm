#ifndef CL_TOKENIZER_H
#define CL_TOKENIZER_H

#include "token.h"

namespace cl
{
    struct CompilationUnit;
    TokenVector tokenize(CompilationUnit &cu);
}

#endif //CL_TOKENIZER_H
