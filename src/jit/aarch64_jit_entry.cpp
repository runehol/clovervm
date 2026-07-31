#include "jit/aarch64_jit_entry.h"

#include "bytecode/code_object.h"
#include "jit/jit_code_object.h"
#include "jit/machine_address_internal.h"
#include "runtime/fatal.h"

#include <cstddef>
#include <cstdint>

namespace cl::jit
{
    extern "C" [[noreturn]] void cl_aarch64_jit_unhandled_side_exit()
    {
        fatal("JIT entered a side exit without a handler");
    }

    static_assert(FrameHeaderCompiledReturnPcOffset * sizeof(Value) == 8);
    static_assert(FrameHeaderSizeAboveFp * sizeof(Value) == 32);

#if defined(__aarch64__)
    extern "C" PRESERVE_NONE Value cl_aarch64_standalone_enter_jit_0(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_standalone_enter_jit_2(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_standalone_enter_jit_4(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_standalone_enter_jit_6(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_standalone_enter_jit_8(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_interpreter_tail_enter_jit_0(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_interpreter_tail_enter_jit_2(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_interpreter_tail_enter_jit_4(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_interpreter_tail_enter_jit_6(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);
    extern "C" PRESERVE_NONE Value cl_aarch64_interpreter_tail_enter_jit_8(
        Value, Value *, const uint8_t *, void *, CodeObject *, ThreadState *);

    namespace
    {
        template <typename Thunk> MachineAddress thunk_address(Thunk thunk)
        {
            return detail::MachineAddressAccess::from_pointer(
                reinterpret_cast<const void *>(thunk));
        }
    }  // namespace
#endif

    MachineAddress
    select_aarch64_interpreter_tail_jit_entry_thunk(uint32_t logical_arity)
    {
#if defined(__aarch64__)
        switch(logical_arity)
        {
            case 0:
                return thunk_address(&cl_aarch64_interpreter_tail_enter_jit_0);
            case 1:
            case 2:
                return thunk_address(&cl_aarch64_interpreter_tail_enter_jit_2);
            case 3:
            case 4:
                return thunk_address(&cl_aarch64_interpreter_tail_enter_jit_4);
            case 5:
            case 6:
                return thunk_address(&cl_aarch64_interpreter_tail_enter_jit_6);
            case 7:
            case 8:
                return thunk_address(&cl_aarch64_interpreter_tail_enter_jit_8);
            default:
                return thunk_address(&cl_aarch64_interpreter_tail_enter_jit_8);
        }
#else
        (void)logical_arity;
        return detail::MachineAddressAccess::from_bits(0);
#endif
    }

    MachineAddress
    select_aarch64_standalone_jit_entry_thunk(uint32_t logical_arity)
    {
#if defined(__aarch64__)
        switch(logical_arity)
        {
            case 0:
                return thunk_address(&cl_aarch64_standalone_enter_jit_0);
            case 1:
            case 2:
                return thunk_address(&cl_aarch64_standalone_enter_jit_2);
            case 3:
            case 4:
                return thunk_address(&cl_aarch64_standalone_enter_jit_4);
            case 5:
            case 6:
                return thunk_address(&cl_aarch64_standalone_enter_jit_6);
            case 7:
            case 8:
                return thunk_address(&cl_aarch64_standalone_enter_jit_8);
            default:
                return thunk_address(&cl_aarch64_standalone_enter_jit_8);
        }
#else
        (void)logical_arity;
        return detail::MachineAddressAccess::from_bits(0);
#endif
    }

    MachineAddress aarch64_jit_unhandled_side_exit_target()
    {
#if defined(__aarch64__)
        return detail::MachineAddressAccess::from_pointer(
            reinterpret_cast<const void *>(
                &cl_aarch64_jit_unhandled_side_exit));
#else
        return detail::MachineAddressAccess::from_bits(0);
#endif
    }

    Value enter_aarch64_jit_from_native(ThreadState &thread, Value *callee_fp,
                                        CodeObject &code_object,
                                        JitCodeObject &jit_code)
    {
#if defined(__aarch64__)
        AArch64StandaloneJitEntryThunk thunk =
            reinterpret_cast<AArch64StandaloneJitEntryThunk>(
                select_aarch64_standalone_jit_entry_thunk(
                    code_object.function_signature.n_parameters)
                    .bits_for_indirect_target());
        if(thunk == nullptr)
        {
            fatal("AArch64 JIT has no standalone native entry thunk");
        }
        return thunk(Value::not_present(), callee_fp, code_object.code.data(),
                     reinterpret_cast<void *>(
                         jit_code.entry().bits_for_indirect_target()),
                     &code_object, &thread);
#else
        (void)thread;
        (void)callee_fp;
        (void)code_object;
        (void)jit_code;
        fatal("AArch64 JIT runtime entry is unavailable on this host");
#endif
    }

}  // namespace cl::jit
