#include "str.h"
#include "class_object.h"
#include "native_function.h"
#include "string_builder.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    void String::install_bootstrap_class(ClassObject *new_cls)
    {
        assert(new_cls != nullptr);
        if(!Object::is_class_bootstrapped())
        {
            Object::install_bootstrap_class(new_cls);
        }
        else
        {
            assert(Object::get_shape()->get_class() == new_cls);
        }
        if(Object::get_shape() == nullptr)
        {
            Object::set_shape(new_cls->get_instance_root_shape());
        }
    }

    static Value native_str_str(Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__str__ expects a str receiver");
        }
        return self;
    }

    static Value native_str_repr(Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__repr__ expects a str receiver");
        }

        String *str = self.get_ptr<String>();
        StringBuilder builder;
        builder.append_char(L'\'');
        size_t n_chars = size_t(str->count.extract());
        for(size_t idx = 0; idx < n_chars; ++idx)
        {
            switch(str->data[idx])
            {
                case L'\\':
                    builder.append_c_str(L"\\\\");
                    break;
                case L'\'':
                    builder.append_c_str(L"\\'");
                    break;
                case L'\n':
                    builder.append_c_str(L"\\n");
                    break;
                case L'\r':
                    builder.append_c_str(L"\\r");
                    break;
                case L'\t':
                    builder.append_c_str(L"\\t");
                    break;
                default:
                    builder.append_char(str->data[idx]);
                    break;
            }
        }
        builder.append_char(L'\'');
        return builder.finish();
    }

    static Value native_str_len(Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__len__ expects a str receiver");
        }
        return self.get_ptr<String>()->count.as_value();
    }

    static Value native_str_add(Value left_value, Value right_value)
    {
        if(!can_convert_to<String>(left_value) ||
           !can_convert_to<String>(right_value))
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"UnimplementedError");
        }

        String *left = left_value.get_ptr<String>();
        String *right = right_value.get_ptr<String>();
        std::wstring result(left->data, size_t(left->count.extract()));
        result.append(right->data, size_t(right->count.extract()));
        return active_thread()->make_object_value<String>(result);
    }

    BuiltinClassDefinition make_str_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::String};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class(
            vm->get_or_create_interned_string_value(L"str"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    void install_str_class_methods(VirtualMachine *vm)
    {
        BuiltinNativeMethod methods[] = {
            builtin_native_method(L"__str__", native_str_str,
                                  L"Return str(self)."),
            builtin_native_method(L"__repr__", native_str_repr,
                                  L"Return repr(self)."),
            builtin_native_method(L"__len__", native_str_len,
                                  L"Return len(self)."),
            builtin_native_method(L"__add__", native_str_add,
                                  L"Return self + value."),
        };
        install_builtin_native_methods(vm, vm->str_class(), methods,
                                       std::size(methods));
    }

    uint64_t string_hash(TValue<String> s)
    {
        String *str = s.extract();
        uint64_t len = str->count.extract();

        cl_wchar *c = &str->data[0];
        uint64_t hash = 5381;
        for(uint64_t i = 0; i < len; ++i)
        {
            hash = hash * 33 + c[i];
        }
        return hash;
    }

    const cl_wchar *string_as_wchar_t(TValue<String> s)
    {
        String *str = s.extract();
        cl_wchar *c = &str->data[0];
        return c;
    }

    bool string_eq_slow_path(TValue<String> a, TValue<String> b)
    {

        const String *sa = a.extract();
        const String *sb = b.extract();

        if(sa->count != sb->count)
            return false;

        uint64_t len = sa->count.extract();

        for(uint64_t i = 0; i < len; ++i)
        {
            if(sa->data[i] != sb->data[i])
                return false;
        }
        return true;
    }

}  // namespace cl
