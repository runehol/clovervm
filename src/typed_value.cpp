#include "typed_value.h"
#include "class_object.h"
#include "str.h"
#include "thread_state.h"
#include <string>

namespace cl
{
    Value raise_exception_for_expected(const wchar_t *type_name,
                                       const wchar_t *message)
    {
        ThreadState *thread = active_thread();
        assert(!thread->has_pending_exception());
        return thread->set_pending_builtin_exception_string(type_name, message);
    }

    Value propagate_exception_for_expected()
    {
        assert(active_thread()->has_pending_exception());
        return Value::exception_marker();
    }

    PropagatedException::PropagatedException()
    {
        assert(active_thread()->has_pending_exception());
    }

    static std::wstring string_to_wstring(TValue<String> string)
    {
        String *str = string.extract();
        return std::wstring(str->data, size_t(str->count.extract()));
    }

    Value set_pending_invalid_typed_value_error(const wchar_t *target_type_name)
    {
        std::wstring message = L"invalid typed value construction for ";
        message += target_type_name;
        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", message.c_str());
    }

    Value set_pending_invalid_typed_value_error(NativeLayoutId native_layout)
    {
        ThreadState *thread = active_thread();
        std::wstring target_type_name = string_to_wstring(
            thread->class_for_native_layout(native_layout)->get_name());
        std::wstring message = L"invalid typed value construction for ";
        message += target_type_name;
        return thread->set_pending_builtin_exception_string(L"TypeError",
                                                            message.c_str());
    }
}  // namespace cl
