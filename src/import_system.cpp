#include "import_system.h"

#include "code_object.h"
#include "codegen.h"
#include "dict.h"
#include "function.h"
#include "list.h"
#include "module_object.h"
#include "owned.h"
#include "parser.h"
#include "slot_dict.h"
#include "source_text.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <filesystem>
#include <system_error>

namespace cl
{
    namespace
    {
        std::wstring string_to_wstring(TValue<String> string)
        {
            return string_as_wchar_t(string);
        }

        bool file_exists(const std::filesystem::path &path)
        {
            std::error_code ec;
            return std::filesystem::is_regular_file(path, ec);
        }

        std::wstring absolute_wstring_path(const std::filesystem::path &path)
        {
            std::error_code ec;
            std::filesystem::path absolute_path =
                std::filesystem::absolute(path, ec);
            if(ec)
            {
                absolute_path = path;
            }
            return absolute_path.lexically_normal().wstring();
        }

        std::optional<ModuleSpec>
        find_source_module_spec_in_path_entry(const std::wstring &module_name,
                                              Value path_entry)
        {
            if(!can_convert_to<String>(path_entry))
            {
                return std::nullopt;
            }

            std::filesystem::path base(string_to_wstring(
                TValue<String>::from_value_assumed(path_entry)));
            if(base.empty())
            {
                base = L".";
            }
            std::filesystem::path package_dir = base / module_name;
            std::filesystem::path package_init = package_dir / L"__init__.py";
            if(file_exists(package_init))
            {
                return ModuleSpec{
                    module_name,
                    absolute_wstring_path(package_init),
                    true,
                    {absolute_wstring_path(package_dir)},
                };
            }

            std::filesystem::path module_file = base / (module_name + L".py");
            if(file_exists(module_file))
            {
                return ModuleSpec{
                    module_name,
                    absolute_wstring_path(module_file),
                    false,
                    {},
                };
            }

            return std::nullopt;
        }

        TValue<String> interned_string(ThreadState *thread,
                                       const std::wstring &text)
        {
            return thread->get_machine()->get_or_create_interned_string_value(
                text);
        }

        TValue<String> interned_string(ThreadState *thread, const wchar_t *text)
        {
            return thread->get_machine()->get_or_create_interned_string_value(
                text);
        }

        void set_module_attr(ThreadState *thread, ModuleObject *module,
                             const wchar_t *name, Value value)
        {
            bool stored =
                module->set_own_property(interned_string(thread, name), value);
            assert(stored);
            (void)stored;
        }

        void remove_imported_module(ThreadState *thread, TValue<String> name)
        {
            Dict *modules = thread->get_machine()->imported_modules().extract();
            if(modules->contains(name.raw_value()))
            {
                Value deleted = modules->del_item(name.raw_value());
                if(deleted.is_exception_marker())
                {
                    thread->clear_pending_exception();
                }
            }
        }

        Value set_module_not_found(ThreadState *thread,
                                   const std::wstring &module_name)
        {
            std::wstring message = L"No module named '";
            message += module_name;
            message += L"'";
            return thread->set_pending_builtin_exception_string(
                L"ModuleNotFoundError", interned_string(thread, message));
        }

        Value set_module_load_failed(ThreadState *thread,
                                     const std::wstring &module_name)
        {
            std::wstring message = L"cannot load module '";
            message += module_name;
            message += L"'";
            return thread->set_pending_builtin_exception_string(
                L"ImportError", interned_string(thread, message));
        }

        void install_module_import_metadata(ThreadState *thread,
                                            ModuleObject *module,
                                            const ModuleSpec &spec)
        {
            set_module_attr(thread, module, L"__doc__", Value::None());
            const std::wstring package_name =
                spec.is_package ? spec.name : std::wstring();
            set_module_attr(thread, module, L"__package__",
                            interned_string(thread, package_name).raw_value());
            set_module_attr(thread, module, L"__loader__", Value::None());
            set_module_attr(thread, module, L"__spec__", Value::None());
            set_module_attr(thread, module, L"__file__",
                            interned_string(thread, spec.origin).raw_value());
            if(spec.is_package)
            {
                Owned<TValue<List>> path(thread->make_object_value<List>());
                for(const std::wstring &location:
                    spec.submodule_search_locations)
                {
                    path.extract()->append(
                        interned_string(thread, location).raw_value());
                }
                set_module_attr(thread, module, L"__path__", path.raw_value());
            }
        }

        Value import_hook_from_module_builtins(ThreadState *thread,
                                               ModuleObject *module)
        {
            Value builtins = module->get_builtins_binding();
            if(builtins.is_not_present())
            {
                builtins =
                    thread->get_machine()->global_builtins_module().raw_value();
            }
            if(!can_convert_to<ModuleObject>(builtins))
            {
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"__builtins__ must be a module");
            }

            TValue<String> import_name = interned_string(thread, L"__import__");
            Value hook =
                builtins.get_ptr<ModuleObject>()->get_own_property(import_name);
            if(hook.is_not_present())
            {
                return thread->set_pending_builtin_exception_string(
                    L"ImportError", L"__import__ not found");
            }
            return hook;
        }
    }  // namespace

    std::optional<ModuleSpec> find_source_module_spec(ThreadState *thread,
                                                      TValue<String> name)
    {
        std::wstring module_name = string_to_wstring(name);
        if(module_name.empty() || module_name.find(L'.') != std::wstring::npos)
        {
            return std::nullopt;
        }

        TValue<String> path_name =
            thread->get_machine()->get_or_create_interned_string_value(L"path");
        Value path_value =
            thread->get_machine()->sys_module().extract()->get_own_property(
                path_name);
        if(!can_convert_to<List>(path_value))
        {
            return std::nullopt;
        }

        List *path = path_value.get_ptr<List>();
        for(size_t path_idx = 0; path_idx < path->size(); ++path_idx)
        {
            std::optional<ModuleSpec> spec =
                find_source_module_spec_in_path_entry(
                    module_name, path->item_unchecked(path_idx));
            if(spec.has_value())
            {
                return spec;
            }
        }

        return std::nullopt;
    }

    Value import_module_absolute(ThreadState *thread, TValue<String> name)
    {
        std::wstring module_name = string_to_wstring(name);
        Dict *modules = thread->get_machine()->imported_modules().extract();
        if(modules->contains(name.raw_value()))
        {
            return modules->get_item(name.raw_value());
        }

        std::optional<ModuleSpec> spec = find_source_module_spec(thread, name);
        if(!spec.has_value())
        {
            return set_module_not_found(thread, module_name);
        }

        Owned<TValue<ModuleObject>> module(
            TValue<ModuleObject>::from_oop(thread->make_module_object(
                name,
                thread->get_machine()->global_builtins_module().raw_value())));
        install_module_import_metadata(thread, module.extract(), *spec);
        modules->set_item(name.raw_value(), module.raw_value());

        try
        {
            std::optional<std::wstring> source =
                read_source_text_file(spec->origin);
            if(!source.has_value())
            {
                remove_imported_module(thread, name);
                return set_module_load_failed(thread, module_name);
            }

            CodeObject *code = thread->compile_in_module(
                source->c_str(), StartRule::File, module.extract(),
                LanguageMode::StandardsCompliant);
            Value result = thread->run_clovervm_code_object(code);
            if(result.is_exception_marker())
            {
                remove_imported_module(thread, name);
                return result;
            }
        }
        catch(...)
        {
            remove_imported_module(thread, name);
            throw;
        }

        return module.raw_value();
    }

    Value import_name_from_code(ThreadState *thread, CodeObject *code_object,
                                TValue<String> name, Value fromlist,
                                int64_t level)
    {
        ModuleObject *module = code_object->get_defining_module().extract();
        Owned<Value> import_hook(
            import_hook_from_module_builtins(thread, module));
        if(import_hook.value().is_exception_marker())
        {
            return import_hook.value();
        }
        if(!can_convert_to<Function>(import_hook.value()))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"object is not callable");
        }

        Owned<Value> globals(
            thread->make_object_value<SlotDict>(module).raw_value());
        Owned<Value> locals(globals.value());
        Owned<Value> owned_fromlist(fromlist);
        Value result = thread->call_clovervm_function(
            TValue<Function>::from_value_assumed(import_hook.value()),
            name.raw_value(), globals.value(), locals.value(),
            owned_fromlist.value(), Value::from_smi(level));
        return result;
    }

}  // namespace cl
