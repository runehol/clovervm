#include "builtins.h"

#include "attr.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "function.h"
#include "list.h"
#include "module_object.h"
#include "native_function.h"
#include "shape.h"
#include "str.h"
#include "thread_state.h"
#include "tuple.h"
#include "typed_value.h"
#include "virtual_machine.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace cl
{
    static bool classinfo_matches(ClassObject *cls, Value classinfo)
    {
        if(can_convert_to<ClassObject>(classinfo))
        {
            return is_subclass_of(
                cls,
                TValue<ClassObject>::from_value_assumed(classinfo).extract());
        }
        if(can_convert_to<Tuple>(classinfo))
        {
            Tuple *tuple = classinfo.get_ptr<Tuple>();
            for(size_t idx = 0; idx < tuple->size(); ++idx)
            {
                if(classinfo_matches(cls, tuple->item_unchecked(idx)))
                {
                    return true;
                }
            }
        }
        return false;
    }

    static bool classinfo_is_valid(Value classinfo)
    {
        if(can_convert_to<ClassObject>(classinfo))
        {
            return true;
        }
        if(!can_convert_to<Tuple>(classinfo))
        {
            return false;
        }
        Tuple *tuple = classinfo.get_ptr<Tuple>();
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            if(!classinfo_is_valid(tuple->item_unchecked(idx)))
            {
                return false;
            }
        }
        return true;
    }

    static Value builtin_isinstance(Value obj, Value classinfo)
    {
        if(!classinfo_is_valid(classinfo))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                L"isinstance() arg 2 must be a type or tuple of types");
        }
        return classinfo_matches(active_thread()->class_of_value(obj),
                                 classinfo)
                   ? Value::True()
                   : Value::False();
    }

    static Value builtin_issubclass(Value cls_value, Value classinfo)
    {
        if(!can_convert_to<ClassObject>(cls_value))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"issubclass() arg 1 must be a type");
        }
        if(!classinfo_is_valid(classinfo))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                L"issubclass() arg 2 must be a type or tuple of types");
        }
        ClassObject *cls =
            TValue<ClassObject>::from_value_assumed(cls_value).extract();
        return classinfo_matches(cls, classinfo) ? Value::True()
                                                 : Value::False();
    }

    static Value require_attribute_name(Value name)
    {
        if(!can_convert_to<String>(name))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"attribute name must be str");
        }
        return Value::None();
    }

    static Value builtin_getattr(Value obj, Value name)
    {
        CL_PROPAGATE_EXCEPTION(require_attribute_name(name));
        Value result = load_attr(obj, TValue<String>::from_value_assumed(name));
        if(result.is_not_present())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"AttributeError", L"object has no such attribute");
        }
        return result;
    }

    static Value builtin_getattr_default(Value obj, Value name,
                                         Value default_tuple)
    {
        CL_PROPAGATE_EXCEPTION(require_attribute_name(name));
        assert(can_convert_to<Tuple>(default_tuple));
        Tuple *defaults = default_tuple.get_ptr<Tuple>();
        if(defaults->size() > 1)
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"TypeError");
        }

        Value result = load_attr(obj, TValue<String>::from_value_assumed(name));
        if(result.is_not_present())
        {
            if(defaults->empty())
            {
                return active_thread()->set_pending_builtin_exception_string(
                    L"AttributeError", L"object has no such attribute");
            }
            return defaults->item_unchecked(0);
        }
        return result;
    }

    static Value builtin_hasattr(Value obj, Value name)
    {
        CL_PROPAGATE_EXCEPTION(require_attribute_name(name));
        Value result = load_attr(obj, TValue<String>::from_value_assumed(name));
        if(result.is_exception_marker())
        {
            return result;
        }
        return result.is_not_present() ? Value::False() : Value::True();
    }

    static Value builtin_setattr(Value obj, Value name, Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_attribute_name(name));
        if(store_attr(obj, TValue<String>::from_value_assumed(name), value))
        {
            return Value::None();
        }
        if(active_thread()->has_pending_exception())
        {
            return Value::exception_marker();
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"AttributeError", L"cannot set attribute");
    }

    static Value builtin_delattr(Value obj, Value name)
    {
        CL_PROPAGATE_EXCEPTION(require_attribute_name(name));
        if(delete_attr(obj, TValue<String>::from_value_assumed(name)))
        {
            return Value::None();
        }
        if(active_thread()->has_pending_exception())
        {
            return Value::exception_marker();
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"AttributeError", L"object has no such attribute");
    }

    static Value builtin_callable(Value obj)
    {
        if(can_convert_to<Function>(obj) || can_convert_to<ClassObject>(obj))
        {
            return Value::True();
        }
        TValue<String> call_name = interned_string(L"__call__");
        return resolve_attr_read_descriptor(obj, call_name).is_found()
                   ? Value::True()
                   : Value::False();
    }

    static Value builtin_ord(Value obj)
    {
        if(!can_convert_to<String>(obj))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"ord() expected string of length 1");
        }
        String *str = obj.get_ptr<String>();
        if(str->count.extract() != 1)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"ord() expected string of length 1");
        }
        return Value::from_smi(static_cast<int64_t>(str->data[0]));
    }

    static Value builtin_chr(Value code_value)
    {
        if(!code_value.is_smi())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"chr() arg must be an integer");
        }
        int64_t code = code_value.get_smi();
        if(code < 0 || code > 0x10ffff)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"chr() arg not in range(0x110000)");
        }
        std::wstring result(1, static_cast<wchar_t>(code));
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    static void append_unique_dir_name(std::vector<std::wstring> &names,
                                       Value name_value)
    {
        if(!can_convert_to<String>(name_value))
        {
            return;
        }
        std::wstring name =
            string_as_wchar_t(TValue<String>::from_value_assumed(name_value));
        if(std::find(names.begin(), names.end(), name) == names.end())
        {
            names.push_back(name);
        }
    }

    static void collect_own_dir_names(std::vector<std::wstring> &names,
                                      Object *object)
    {
        AttributeMappingEntry entry;
        Shape *shape = object->get_shape();
        for(uint32_t idx = 0; idx < shape->present_count(); ++idx)
        {
            if(own_attribute_mapping_entry_at(object, idx, entry))
            {
                append_unique_dir_name(names, entry.key);
            }
        }
    }

    static void collect_mro_dir_names(std::vector<std::wstring> &names,
                                      ClassObject *cls)
    {
        Value mro_value = cls->get_mro_value();
        if(!can_convert_to<Tuple>(mro_value))
        {
            collect_own_dir_names(names, cls);
            return;
        }
        Tuple *mro = mro_value.get_ptr<Tuple>();
        for(size_t idx = 0; idx < mro->size(); ++idx)
        {
            Value cls_value = mro->item_unchecked(idx);
            if(can_convert_to<ClassObject>(cls_value))
            {
                collect_own_dir_names(
                    names, TValue<ClassObject>::from_value_assumed(cls_value)
                               .extract());
            }
        }
    }

    static Value builtin_dir(Value obj)
    {
        std::vector<std::wstring> names;
        if(obj.is_ptr())
        {
            collect_own_dir_names(names, obj.get_ptr<Object>());
        }

        if(can_convert_to<ClassObject>(obj))
        {
            collect_mro_dir_names(
                names, TValue<ClassObject>::from_value_assumed(obj).extract());
        }
        else
        {
            collect_mro_dir_names(names, active_thread()->class_of_value(obj));
        }

        std::sort(names.begin(), names.end());
        List *list = make_object_raw<List>();
        for(const std::wstring &name: names)
        {
            list->append(active_vm()
                             ->get_or_create_interned_string_value(name)
                             .raw_value());
        }
        return Value::from_oop(list);
    }

    static void install_builtin_function_binding(VirtualMachine *vm,
                                                 const wchar_t *name,
                                                 Value value)
    {
        bool installed =
            vm->global_builtins_module().extract()->set_own_property(
                vm->get_or_create_interned_string_value(name), value);
        assert(installed);
        (void)installed;
    }

    void install_builtin_function_bindings(VirtualMachine *vm)
    {
        install_builtin_function_binding(
            vm, L"isinstance",
            make_intrinsic_function(vm, builtin_isinstance).raw_value());
        install_builtin_function_binding(
            vm, L"issubclass",
            make_intrinsic_function(vm, builtin_issubclass).raw_value());
        install_builtin_function_binding(
            vm, L"setattr",
            make_intrinsic_function(vm, builtin_setattr).raw_value());
        install_builtin_function_binding(
            vm, L"delattr",
            make_intrinsic_function(vm, builtin_delattr).raw_value());
        install_builtin_function_binding(
            vm, L"callable",
            make_intrinsic_function(vm, builtin_callable).raw_value());
        install_builtin_function_binding(
            vm, L"ord", make_intrinsic_function(vm, builtin_ord).raw_value());
        install_builtin_function_binding(
            vm, L"chr", make_intrinsic_function(vm, builtin_chr).raw_value());
        install_builtin_function_binding(
            vm, L"_clover_builtin_getattr",
            make_intrinsic_function(vm, builtin_getattr).raw_value());
        install_builtin_function_binding(
            vm, L"_clover_builtin_getattr_default",
            make_intrinsic_function(vm, builtin_getattr_default).raw_value());
        install_builtin_function_binding(
            vm, L"_clover_builtin_hasattr",
            make_intrinsic_function(vm, builtin_hasattr).raw_value());
        install_builtin_function_binding(
            vm, L"_clover_builtin_dir",
            make_intrinsic_function(vm, builtin_dir).raw_value());
    }
}  // namespace cl
