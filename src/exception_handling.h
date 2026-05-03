#ifndef CL_EXCEPTION_HANDLING_H
#define CL_EXCEPTION_HANDLING_H

#include "value.h"

#include <cstdint>

namespace cl
{
    struct CodeObject;

    struct ExceptionalTarget
    {
        Value *fp;
        CodeObject *code_object;
        const uint8_t *interpreted_pc;
        const void *jit_pc = nullptr;
    };

}  // namespace cl

#endif  // CL_EXCEPTION_HANDLING_H
