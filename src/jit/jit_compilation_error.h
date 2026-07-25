#ifndef CL_JIT_JIT_COMPILATION_ERROR_H
#define CL_JIT_JIT_COMPILATION_ERROR_H

#include "jit/code_cache_types.h"
#include "jit/register_allocator.h"

#include <variant>

namespace cl::jit
{
    using JitCompilationError =
        std::variant<RegisterAllocationError, JitCodeError>;

}  // namespace cl::jit

#endif  // CL_JIT_JIT_COMPILATION_ERROR_H
