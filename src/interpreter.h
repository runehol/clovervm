#ifndef CL_INTERPRETER_H
#define CL_INTERPRETER_H

#include <cstdint>
#include "value.h"

namespace cl
{
    struct CodeObject;

    Value run_interpreter(Value *stack_frame, const CodeObject *code_object, uint32_t start_pc);
}

#endif //CL_INTERPRETER_H
