#include "startup_wrapper.h"

#include "code_object.h"
#include "code_object_builder.h"
#include "runtime_helpers.h"

namespace cl
{
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

}  // namespace cl
