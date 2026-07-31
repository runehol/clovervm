#include "jit/aarch64_jit_entry.h"

#include "bytecode/code_object.h"
#include "jit/jit_code_object.h"
#include "jit/machine_address_internal.h"
#include "runtime/fatal.h"

#include <cstddef>
#include <cstdint>

namespace cl::jit
{
    static_assert(FrameHeaderCompiledReturnPcOffset * sizeof(Value) == 8);
    static_assert(FrameHeaderSizeAboveFp * sizeof(Value) == 32);

#if defined(__aarch64__)
    extern "C" Value cl_aarch64_enter_jit_0(ThreadState *, Value *,
                                            CodeObject *, uintptr_t);
    extern "C" Value cl_aarch64_enter_jit_2(ThreadState *, Value *,
                                            CodeObject *, uintptr_t);
    extern "C" Value cl_aarch64_enter_jit_4(ThreadState *, Value *,
                                            CodeObject *, uintptr_t);
    extern "C" Value cl_aarch64_enter_jit_6(ThreadState *, Value *,
                                            CodeObject *, uintptr_t);
    extern "C" Value cl_aarch64_enter_jit_8(ThreadState *, Value *,
                                            CodeObject *, uintptr_t);

    namespace
    {
        MachineAddress thunk_address(AArch64JitEntryThunk thunk)
        {
            return detail::MachineAddressAccess::from_pointer(
                reinterpret_cast<const void *>(thunk));
        }
    }  // namespace
#endif

    MachineAddress select_aarch64_jit_entry_thunk(uint32_t logical_arity)
    {
        if(logical_arity > 8)
        {
            fatal("AArch64 JIT entry does not support more than eight "
                  "parameters");
        }

#if defined(__aarch64__)
        switch(logical_arity)
        {
            case 0:
                return thunk_address(&cl_aarch64_enter_jit_0);
            case 1:
            case 2:
                return thunk_address(&cl_aarch64_enter_jit_2);
            case 3:
            case 4:
                return thunk_address(&cl_aarch64_enter_jit_4);
            case 5:
            case 6:
                return thunk_address(&cl_aarch64_enter_jit_6);
            case 7:
            case 8:
                return thunk_address(&cl_aarch64_enter_jit_8);
        }
        fatal("invalid AArch64 JIT entry arity");
#else
        return detail::MachineAddressAccess::from_bits(0);
#endif
    }

    Value enter_aarch64_jit(ThreadState &thread, Value *callee_fp,
                            CodeObject &code_object, JitCodeObject &jit_code)
    {
#if defined(__aarch64__)
        AArch64JitEntryThunk thunk = reinterpret_cast<AArch64JitEntryThunk>(
            jit_code.interpreter_entry_thunk().bits_for_indirect_target());
        if(thunk == nullptr)
        {
            fatal("AArch64 JIT code has no interpreter entry thunk");
        }
        return thunk(&thread, callee_fp, &code_object,
                     jit_code.entry().bits_for_indirect_target());
#else
        (void)thread;
        (void)callee_fp;
        (void)code_object;
        (void)jit_code;
        fatal("AArch64 JIT runtime entry is unavailable on this host");
#endif
    }

}  // namespace cl::jit
