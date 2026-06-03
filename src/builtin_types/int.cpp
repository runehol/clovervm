#include "builtin_types/int.h"

#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/owned.h"
#include "object_model/value.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cwctype>
#include <iterator>
#include <string>

namespace cl
{
    static Value invalid_int_literal(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"ValueError", L"invalid literal for int()");
    }

    static bool is_ascii_digit(cl_wchar ch) { return ch >= L'0' && ch <= L'9'; }

    static Value parse_int_string(ThreadState *thread, TValue<String> string)
    {
        String *str = string.extract();
        size_t begin = 0;
        size_t end = size_t(str->count.extract());
        while(begin < end && std::iswspace(str->data[begin]))
        {
            ++begin;
        }
        while(begin < end && std::iswspace(str->data[end - 1]))
        {
            --end;
        }
        if(begin == end)
        {
            return invalid_int_literal(thread);
        }

        bool negative = false;
        if(str->data[begin] == L'+' || str->data[begin] == L'-')
        {
            negative = str->data[begin] == L'-';
            ++begin;
        }

        static constexpr uint64_t positive_limit =
            static_cast<uint64_t>(value_smi_max);
        static constexpr uint64_t negative_limit = positive_limit + 1;
        uint64_t limit = negative ? negative_limit : positive_limit;
        uint64_t parsed = 0;
        bool saw_digit = false;
        bool previous_underscore = false;
        for(size_t idx = begin; idx < end; ++idx)
        {
            cl_wchar ch = str->data[idx];
            if(ch == L'_')
            {
                if(!saw_digit || previous_underscore)
                {
                    return invalid_int_literal(thread);
                }
                previous_underscore = true;
                continue;
            }
            if(!is_ascii_digit(ch))
            {
                return invalid_int_literal(thread);
            }

            uint64_t digit = static_cast<uint64_t>(ch - L'0');
            if(parsed > (limit - digit) / 10)
            {
                return thread->set_pending_builtin_exception_string(
                    L"OverflowError", L"integer overflow");
            }
            parsed = parsed * 10 + digit;
            saw_digit = true;
            previous_underscore = false;
        }
        if(!saw_digit || previous_underscore)
        {
            return invalid_int_literal(thread);
        }

        int64_t result = static_cast<int64_t>(parsed);
        if(negative)
        {
            result = -result;
        }
        return Value::from_smi(result);
    }

    static Value native_int_new(ThreadState *thread, Value cls_value, Value obj)
    {
        if(cls_value != Value::from_oop(active_vm()->int_class()))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"int.__new__ expects int as cls");
        }

        if((obj.as.integer & value_not_smi_or_boolean_mask) == 0)
        {
            Value result;
            result.as.integer = obj.as.integer & value_boolean_to_integer_mask;
            return result;
        }

        if(can_convert_to<String>(obj))
        {
            return parse_int_string(thread,
                                    TValue<String>::from_value_assumed(obj));
        }

        return thread->set_pending_builtin_exception_string(
            L"TypeError",
            L"int conversion is only implemented for int, bool and str");
    }

    static Value native_int_str(ThreadState *thread, Value self)
    {
        if(!self.is_smi())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"int.__str__ expects an int receiver");
        }
        return active_thread()
            ->make_object_value<String>(std::to_wstring(self.get_smi()))
            .raw_value();
    }

    BuiltinClassDefinition make_int_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"int"), 0, nullptr, 0,
            vm->object_class(), NativeLayoutId::Invalid);
        return builtin_class_definition(cls, BuiltinsVisibility::Public);
    }

    void install_int_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> int_new_defaults(
            active_thread()->make_object_value<Tuple>(1));
        int_new_defaults.extract()->initialize_item_unchecked(
            0, Value::from_smi(0));
        BuiltinIntrinsicMethod methods[] = {
            with_defaults(builtin_intrinsic_method(L"__new__", native_int_new,
                                                   L"Create an int object."),
                          int_new_defaults.value()),
            builtin_intrinsic_method(L"__str__", native_int_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_int_str,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->int_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
