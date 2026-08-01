#ifndef CL_AARCH64_JIT_ENTRY_H
#define CL_AARCH64_JIT_ENTRY_H

#include "jit/machine_address.h"
#include "object_model/value.h"
#include "util/compiler.h"

#include <cstdint>

namespace cl
{
    class CodeObject;
    class ThreadState;

    namespace jit
    {
        class JitCodeObject;

        using AArch64StandaloneJitEntryThunk =
            Value(PRESERVE_NONE *)(Value, Value *, const uint8_t *, void *,
                                   CodeObject *, ThreadState *);

        MachineAddress
        select_aarch64_interpreter_tail_jit_entry_thunk(uint32_t logical_arity);

        MachineAddress
        select_aarch64_standalone_jit_entry_thunk(uint32_t logical_arity);

        MachineAddress aarch64_jit_side_exit_target();

        [[nodiscard]] Value
        enter_aarch64_jit_from_native(ThreadState &thread, Value *callee_fp,
                                      CodeObject &code_object,
                                      JitCodeObject &jit_code);

    }  // namespace jit
}  // namespace cl

#endif  // CL_AARCH64_JIT_ENTRY_H
