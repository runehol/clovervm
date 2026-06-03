#ifndef CL_EXCEPTION_HANDLING_H
#define CL_EXCEPTION_HANDLING_H

#include "object_model/value.h"

#include <cstdint>

namespace cl
{
    class CodeObject;

    struct ExceptionalTarget
    {
        Value *fp;
        CodeObject *code_object;
        const uint8_t *interpreted_pc;
        const void *jit_pc = nullptr;
    };

}  // namespace cl

#endif  // CL_EXCEPTION_HANDLING_H
