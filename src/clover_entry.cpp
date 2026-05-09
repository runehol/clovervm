#include "clover_entry.h"

#include "code_object.h"
#include "code_object_builder.h"
#include "runtime_helpers.h"
#include "scope.h"
#include "virtual_machine.h"
#include <stdexcept>

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

    TValue<CodeObject>
    make_startup_wrapper_code_object(CodeObject *entry_code_object)
    {
        TValue<String> wrapper_name(interned_string(L"<startup_wrapper>"));
        CodeObjectBuilder code(entry_code_object->compilation_unit,
                               entry_code_object->module_scope.extract(),
                               nullptr, wrapper_name);

        uint32_t entry_const_idx =
            code.allocate_constant(Value::from_oop(entry_code_object));
        JumpTarget handler(&code);
        ExceptionTableRangeBuilder range(&code, handler);

        code.emit_call_code_object(0, entry_const_idx, OutgoingArgReg(0), 0);
        range.close();
        code.emit_halt(0);

        handler.resolve();
        code.emit_raise_if_unhandled_exception(0);
        code.emit_halt(0);
        return TValue<CodeObject>::from_oop(code.finalize());
    }

    CodeObject *
    make_clover_function_entry_adapter_code_object(VirtualMachine *vm,
                                                   uint32_t n_args)
    {
        if(n_args > MaxCloverFunctionEntryAdapterArgs)
        {
            throw std::runtime_error(
                "unsupported Clover function entry adapter arity");
        }

        TValue<String> adapter_name = vm->get_or_create_interned_string_value(
            L"<clover_function_entry_adapter>");
        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(vm, nullptr, nullptr, local_scope, adapter_name);
        code.n_parameters() = n_args + 1;
        code.n_positional_parameters() = n_args + 1;
        reserve_parameter_slots_and_frame_header(&code);

        {
            CodeObjectBuilder::TemporaryReg callable_reg(code);
            code.emit_ldar(0, 0);
            code.emit_star(0, callable_reg);

            for(uint32_t arg_idx = 0; arg_idx < n_args; ++arg_idx)
            {
                code.emit_ldar(0, arg_idx + 1);
                code.emit_star(0, OutgoingArgReg(arg_idx));
            }

            JumpTarget handler(&code);
            ExceptionTableRangeBuilder range(&code, handler);
            code.emit_call_simple(0, callable_reg, OutgoingArgReg(0),
                                  uint8_t(n_args));
            range.close();
            code.emit_return_to_native(0);

            handler.resolve();
            code.emit_return_pending_exception_to_native(0);
        }
        return code.finalize();
    }

}  // namespace cl
