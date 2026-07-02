#include "runtime/protocol_helper_functions.h"

#include "builtin_types/module_object.h"
#include "bytecode/code_object.h"
#include "bytecode/code_object_builder.h"
#include "compiler/scope.h"
#include "object_model/function.h"
#include "runtime/exception_object.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

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

    static void configure_positional_helper(CodeObjectBuilder &code,
                                            uint32_t n_parameters)
    {
        code.n_parameters() = n_parameters;
        code.n_positional_parameters() = n_parameters;
        code.function_signature().n_pos_or_kw_parameters = n_parameters;
        reserve_parameter_slots_and_frame_header(&code);
    }

    static Expected<TValue<Function>>
    finish_helper_function(VirtualMachine *vm, CodeObjectBuilder &code)
    {
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                TValue<CodeObject>::from_oop(CL_TRY(code.finalize())),
                Optional<TValue<String>>::none()));
    }

    Expected<TValue<Function>>
    make_hash_value_helper_function(VirtualMachine *vm)
    {
        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        TValue<String> name =
            vm->get_or_create_interned_string_value(L"<hash_value_helper>");
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(),
                               local_scope, name);
        configure_positional_helper(code, 1);

        uint8_t hash_name_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"__hash__").raw_value()));
        uint8_t type_error_idx = CL_TRY(code.allocate_constant(Value::from_oop(
            vm->get_default_thread()->class_for_builtin_name(L"TypeError"))));
        uint8_t unhashable_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"object is unhashable")
                .raw_value()));

        {
            CodeObjectBuilder::TemporaryReg call_args(
                code, 1, RegisterAlignment::CallFrame);
            CL_TRY(code.emit_mov(0, call_args, 0));
            CL_TRY(code.emit_call_special_method(0, call_args, hash_name_idx, 0,
                                                 type_error_idx,
                                                 unhashable_idx));
        }
        CL_TRY(code.emit_unary_op(0, Bytecode::CanonicalizeHash,
                                  OperatorBytecodeFormat::Plain));
        CL_TRY(code.emit_return(0));

        return finish_helper_function(vm, code);
    }

    Expected<TValue<Function>>
    make_test_equal_helper_function(VirtualMachine *vm)
    {
        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        TValue<String> name =
            vm->get_or_create_interned_string_value(L"<test_equal_helper>");
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(),
                               local_scope, name);
        configure_positional_helper(code, 2);

        CL_TRY(code.emit_ldar(0, 1));
        CL_TRY(code.emit_operator_reg(
            0, Bytecode::TestEqual, 0,
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck));
        CL_TRY(code.emit_to_bool(0));
        CL_TRY(code.emit_return(0));

        return finish_helper_function(vm, code);
    }

}  // namespace cl
