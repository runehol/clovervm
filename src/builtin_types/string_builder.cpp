#include "builtin_types/string_builder.h"

#include "object_model/class_object.h"
#include "object_model/value_string.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"

namespace cl
{
    void StringBuilder::append_char(cl_wchar ch) { buffer += ch; }

    void StringBuilder::append_c_str(const cl_wchar *text) { buffer += text; }

    void StringBuilder::append_string(TValue<String> string)
    {
        String *str = string.extract();
        buffer.append(str->data, size_t(str->count.extract()));
    }

    Value StringBuilder::append_repr(Value value)
    {
        Value str = value_to_repr_string(value);
        CL_PROPAGATE_EXCEPTION(str);
        append_string(CL_TRY(TValue<String>::from_value_checked(str)));
        return Value::None();
    }

    Value StringBuilder::append_str(Value value)
    {
        Value str = value_to_str_string(value);
        CL_PROPAGATE_EXCEPTION(str);
        append_string(CL_TRY(TValue<String>::from_value_checked(str)));
        return Value::None();
    }

    Value StringBuilder::finish()
    {
        return active_thread()->make_object_value<String>(buffer).raw_value();
    }

}  // namespace cl
