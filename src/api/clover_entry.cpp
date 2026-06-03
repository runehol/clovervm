#include "api/clover_entry.h"

#include "builtin_types/module_object.h"
#include "bytecode/code_object.h"
#include "bytecode/code_object_builder.h"
#include "compiler/scope.h"
#include "runtime/runtime_helpers.h"
#include "runtime/virtual_machine.h"
#include <algorithm>

namespace cl
{
    static void
    reserve_parameter_slots_and_frame_header(CodeObjectBuilder *code)
    {
        Scope *local_scope = code->get_local_scope_ptr();
        local_scope->reserve_empty_slots(code->n_parameters());
        uint32_t n_parameter_padding =
            code->get_padded_n_parameters() - code->n_parameters();
        local_scope->reserve_empty_slots(n_parameter_padding);
        local_scope->reserve_empty_slots(FrameHeaderSize);
    }

    Expected<CodeObject *>
    make_clover_function_entry_adapter_code_object(VirtualMachine *vm,
                                                   uint32_t n_args)
    {
        if(n_args > MaxCloverFunctionEntryAdapterArgs)
        {
            return Expected<CodeObject *>::raise_exception(
                L"SystemError",
                L"unsupported Clover function entry adapter arity");
        }

        TValue<String> adapter_name = vm->get_or_create_interned_string_value(
            L"<clover_function_entry_adapter>");
        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(),
                               local_scope, adapter_name);
        code.n_parameters() = n_args + 1;
        code.n_positional_parameters() = n_args + 1;
        code.function_signature().n_pos_or_kw_parameters = n_args + 1;
        reserve_parameter_slots_and_frame_header(&code);

        {
            CodeObjectBuilder::TemporaryReg callable_reg(code);
            CL_TRY(code.emit_mov(0, callable_reg, 0));

            CodeObjectBuilder::TemporaryReg call_args(
                code, std::max<uint32_t>(n_args, 1),
                RegisterAlignment::CallFrame);
            for(uint32_t arg_idx = 0; arg_idx < n_args; ++arg_idx)
            {
                CL_TRY(code.emit_mov(0, call_args + arg_idx, arg_idx + 1));
            }

            JumpTarget handler(&code);
            ExceptionTableRangeBuilder range(&code, handler);
            CL_TRY(code.emit_call_positional(0, callable_reg, call_args,
                                             uint8_t(n_args)));
            range.close();
            CL_TRY(code.emit_return_to_native(0));

            CL_TRY(handler.resolve());
            CL_TRY(code.emit_return_exception_marker_to_native(0));
        }
        return code.finalize();
    }

}  // namespace cl
