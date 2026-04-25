#include "str.h"
#include "builtin_function.h"
#include "class_object.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>
#include <stdexcept>

namespace cl
{
    void String::install_bootstrap_class(ClassObject *new_cls)
    {
        assert(new_cls != nullptr);
        if(Object::get_class() == nullptr)
        {
            Object::install_bootstrap_class(new_cls);
        }
        else
        {
            assert(Object::get_class() == new_cls);
        }
    }

    static Value builtin_str_str(ThreadState *, const CallArguments &args)
    {
        if(args.n_args != 1 || !can_convert_to<String>(args[0]))
        {
            throw std::runtime_error(
                "TypeError: str.__str__ expects a str receiver");
        }
        return args[0];
    }

    static Value builtin_str_add(ThreadState *thread, const CallArguments &args)
    {
        if(args.n_args != 2 || !can_convert_to<String>(args[0]) ||
           !can_convert_to<String>(args[1]))
        {
            throw std::runtime_error(
                "TypeError: str.__add__ expects two str arguments");
        }

        String *left = args[0].get_ptr<String>();
        String *right = args[1].get_ptr<String>();
        std::wstring result(left->data, size_t(left->count.extract()));
        result.append(right->data, size_t(right->count.extract()));
        return thread->make_refcounted_object_value<String>(result);
    }

    BuiltinClassDefinition make_str_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::String};
        BuiltinClassMethod methods[] = {
            BuiltinClassMethod{
                vm->get_or_create_interned_string_value(L"__str__"),
                Value::from_oop(vm->make_immortal_object_raw<BuiltinFunction>(
                    builtin_str_str, 1, 1))},
            BuiltinClassMethod{
                vm->get_or_create_interned_string_value(L"__add__"),
                Value::from_oop(vm->make_immortal_object_raw<BuiltinFunction>(
                    builtin_str_add, 2, 2))},
        };

        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"str"), 1, methods,
            std::size(methods));
        return builtin_class_definition(cls, native_layout_ids);
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
