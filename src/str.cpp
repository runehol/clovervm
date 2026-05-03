#include "str.h"
#include "class_object.h"
#include "native_function.h"
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
            assert(Object::get_class().extract() == new_cls);
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
        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        ClassObject *cls = vm->str_class();
        ShapeFlags class_shape_flags = cls->get_shape()->flags();
        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags & ~fixed_attribute_shape_flags()));
        bool stored = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__str__"),
            make_native_function(vm, native_str_str), method_flags);
        assert(stored);
        stored = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__add__"),
            make_native_function(vm, native_str_add), method_flags);
        assert(stored);
        (void)stored;
        cls->set_shape(cls->get_shape()->clone_with_flags(class_shape_flags));
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
