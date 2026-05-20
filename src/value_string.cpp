#include "value_string.h"

#include "attr.h"
#include "exception_propagation.h"
#include "function.h"
#include "runtime_helpers.h"
#include "str.h"
#include "thread_state.h"

namespace cl
{
    static Value value_to_dunder_string(Value value, const wchar_t *name,
                                        const wchar_t *error_message)
    {
        TValue<String> method_name =
            active_vm()->get_or_create_interned_string_value(name);
        Value callable;
        Value self;
        if(!load_special_method(value, method_name, callable, self))
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"AttributeError");
        }
        if(!can_convert_to<Function>(callable))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"object is not callable");
        }

        Value result =
            self.is_not_present()
                ? active_thread()->call_clovervm_function(
                      TValue<Function>::from_value_assumed(callable))
                : active_thread()->call_clovervm_function(
                      TValue<Function>::from_value_assumed(callable), self);
        CL_PROPAGATE_EXCEPTION(result);
        if(!can_convert_to<String>(result))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", error_message);
        }
        return result;
    }

    Value value_to_repr_string(Value value)
    {
        return value_to_dunder_string(value, L"__repr__",
                                      L"__repr__ returned non-str");
    }

    Value value_to_str_string(Value value)
    {
        return value_to_dunder_string(value, L"__str__",
                                      L"__str__ returned non-str");
    }

}  // namespace cl
