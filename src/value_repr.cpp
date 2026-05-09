#include "value_repr.h"

#include "exception_propagation.h"
#include "runtime_helpers.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"

namespace cl
{
    Value value_repr(Value value)
    {
        TValue<String> repr_name =
            active_vm()->get_or_create_interned_string_value(L"__repr__");
        Value repr_value =
            active_thread()->call_clovervm_method(value, repr_name);
        CL_PROPAGATE_EXCEPTION(repr_value);
        if(!can_convert_to<String>(repr_value))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"__repr__ returned non-str");
        }
        return repr_value;
    }

    Value append_value_repr(std::wstring &out, Value value)
    {
        Value repr_value = value_repr(value);
        CL_PROPAGATE_EXCEPTION(repr_value);

        String *repr = repr_value.get_ptr<String>();
        out.append(repr->data, size_t(repr->count.extract()));
        return Value::None();
    }

}  // namespace cl
