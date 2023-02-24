#ifndef CL_INTERPRETER_H
#define CL_INTERPRETER_H

#include <cstdint>
#include "cl_value.h"

namespace cl
{
    struct CodeObject;

    CLValue run_interpreter(const CodeObject *code_object, uint32_t start_pc);
}

#endif //CL_INTERPRETER_H