#include "runtime/protocol_helper_functions.h"

#include "builtin_types/module_object.h"
#include "bytecode/code_object_builder.h"
#include "compiler/scope.h"
#include "object_model/function.h"
#include "runtime/exception_propagation.h"
#include "runtime/virtual_machine.h"

namespace cl
{
    Expected<TValue<Function>>
    make_hash_value_helper_function(VirtualMachine *vm)
    {
        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        TValue<String> name =
            vm->get_or_create_interned_string_value(L"<hash_value_helper>");
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(),
                               local_scope, name);
        code.configure_positional_function(1);

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

        return code.finalize_immortal_function(vm);
    }

    Expected<TValue<Function>>
    make_test_equal_helper_function(VirtualMachine *vm)
    {
        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        TValue<String> name =
            vm->get_or_create_interned_string_value(L"<test_equal_helper>");
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(),
                               local_scope, name);
        code.configure_positional_function(2);

        CL_TRY(code.emit_ldar(0, 1));
        CL_TRY(code.emit_operator_reg(
            0, Bytecode::TestEqual, 0,
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck));
        CL_TRY(code.emit_to_bool(0));
        CL_TRY(code.emit_return(0));

        return code.finalize_immortal_function(vm);
    }

}  // namespace cl
