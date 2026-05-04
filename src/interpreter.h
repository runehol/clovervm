#ifndef CL_INTERPRETER_H
#define CL_INTERPRETER_H

#include "value.h"
#include <cstdint>

namespace cl
{
    struct CodeObject;
    class ThreadState;

    Value run_interpreter(Value *stack_frame, CodeObject *code_object,
                          uint32_t start_pc, ThreadState *thread);
}  // namespace cl

#endif  // CL_INTERPRETER_H
