#ifndef CL_AARCH64_JIT_ENTRY_H
#define CL_AARCH64_JIT_ENTRY_H

#include "jit/machine_address.h"
#include "object_model/value.h"

#include <cstdint>

namespace cl
{
    class CodeObject;
    class ThreadState;

    namespace jit
    {
        class JitCodeObject;

        using AArch64JitEntryThunk = Value (*)(ThreadState *, Value *,
                                               CodeObject *, uintptr_t);

        MachineAddress select_aarch64_jit_entry_thunk(uint32_t logical_arity);

        [[nodiscard]] Value enter_aarch64_jit(ThreadState &thread,
                                              Value *callee_fp,
                                              CodeObject &code_object,
                                              JitCodeObject &jit_code);

    }  // namespace jit
}  // namespace cl

#endif  // CL_AARCH64_JIT_ENTRY_H
