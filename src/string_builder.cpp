#include "string_builder.h"

#include "class_object.h"
#include "exception_propagation.h"
#include "thread_state.h"
#include "value_string.h"

namespace cl
{
    void StringBuilder::append_char(cl_wchar ch) { buffer += ch; }

    void StringBuilder::append_c_str(const cl_wchar *text) { buffer += text; }

    void StringBuilder::append_string(TValue2<String> string)
    {
        String *str = string.extract();
        buffer.append(str->data, size_t(str->count.extract()));
    }

    Value StringBuilder::append_repr(Value value)
    {
        Value str = value_to_repr_string(value);
        CL_PROPAGATE_EXCEPTION(str);
        append_string(CL_TRY(TValue2<String>::from_value_checked(str)));
        return Value::None();
    }

    Value StringBuilder::append_str(Value value)
    {
        Value str = value_to_str_string(value);
        CL_PROPAGATE_EXCEPTION(str);
        append_string(CL_TRY(TValue2<String>::from_value_checked(str)));
        return Value::None();
    }

    Value StringBuilder::finish()
    {
        return active_thread()->make_object_value<String>(buffer);
    }

}  // namespace cl
