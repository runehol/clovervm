#include "import_system.h"

#include "class_object.h"
#include "code_object.h"
#include "codegen.h"
#include "dict.h"
#include "exception_object.h"
#include "function.h"
#include "list.h"
#include "module_finder.h"
#include "module_loader_object.h"
#include "module_object.h"
#include "module_spec_object.h"
#include "owned.h"
#include "parser.h"
#include "slot_dict.h"
#include "source_text.h"
#include "str.h"
#include "thread_state.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <vector>

namespace cl
{
    namespace
    {
        std::wstring string_to_wstring(TValue<String> string)
        {
            return string_as_wchar_t(string);
        }

        TValue<String> interned_string(ThreadState *thread,
                                       const std::wstring &text)
        {
            return thread->get_machine()->get_or_create_interned_string_value(
                text);
        }

        std::vector<std::wstring> split_module_name(const std::wstring &name)
        {
            std::vector<std::wstring> components;
            size_t start = 0;
            while(start <= name.size())
            {
                size_t dot = name.find(L'.', start);
                size_t end = dot == std::wstring::npos ? name.size() : dot;
                components.push_back(name.substr(start, end - start));
                if(dot == std::wstring::npos)
                {
                    break;
                }
                start = dot + 1;
            }
            return components;
        }

        std::wstring parent_module_name(const std::wstring &name)
        {
            size_t dot = name.rfind(L'.');
            if(dot == std::wstring::npos)
            {
                return std::wstring();
            }
            return name.substr(0, dot);
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

        Value get_cached_module(ThreadState *thread, TValue<String> name)
        {
            Dict *modules = thread->get_machine()->imported_modules().extract();
            if(!modules->contains(name.raw_value()))
            {
                return Value::not_present();
            }

            Value module = modules->get_item(name.raw_value());
            if(module == Value::None())
            {
                return set_module_not_found(thread, string_to_wstring(name));
            }
            return module;
        }

        Value set_not_a_package(ThreadState *thread,
                                const std::wstring &module_name,
                                const std::wstring &parent_name)
        {
            std::wstring message = L"No module named '";
            message += module_name;
            message += L"'; '";
            message += parent_name;
            message += L"' is not a package";
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

        Value set_cannot_import_name(ThreadState *thread, Value module,
                                     TValue<String> name)
        {
            std::wstring message = L"cannot import name '";
            message += string_to_wstring(name);
            message += L"' from '";
            if(can_convert_to<ModuleObject>(module))
            {
                Value module_name =
                    module.get_ptr<ModuleObject>()->get_name_binding();
                if(can_convert_to<String>(module_name))
                {
                    message += string_to_wstring(
                        TValue<String>::from_value_assumed(module_name));
                }
                else
                {
                    message += L"<unknown>";
                }
            }
            else
            {
                message += L"<unknown>";
            }
            message += L"'";
            return thread->set_pending_builtin_exception_string(
                L"ImportError", interned_string(thread, message));
        }

        std::wstring module_name_for_message(Value module)
        {
            if(can_convert_to<ModuleObject>(module))
            {
                Value module_name =
                    module.get_ptr<ModuleObject>()->get_name_binding();
                if(can_convert_to<String>(module_name))
                {
                    return string_to_wstring(
                        TValue<String>::from_value_assumed(module_name));
                }
            }
            return L"<unknown>";
        }

        bool pending_exception_is_stop_iteration(ThreadState *thread)
        {
            switch(thread->pending_exception_kind())
            {
                case PendingExceptionKind::StopIteration:
                    return true;
                case PendingExceptionKind::Object:
                    return is_subclass_of(thread->pending_exception_object()
                                              .extract()
                                              ->get_shape()
                                              ->get_class(),
                                          thread->class_for_native_layout(
                                              NativeLayoutId::StopIteration));
                case PendingExceptionKind::None:
                    return false;
            }
            __builtin_unreachable();
        }

        Value set_bad_all_item(ThreadState *thread,
                               const std::wstring &module_name, Value item)
        {
            std::wstring message = L"Item in ";
            message += module_name;
            message += L".__all__ must be str, not ";
            message +=
                string_to_wstring(thread->class_of_value(item)->get_name());
            return thread->set_pending_builtin_exception_string(
                L"TypeError", interned_string(thread, message));
        }

        bool string_starts_with_underscore(TValue<String> name)
        {
            String *str = name.extract();
            return str->count.extract() > 0 && str->data[0] == L'_';
        }

        void install_package_import_metadata(ThreadState *thread,
                                             ModuleObject *module,
                                             const ModuleSpec &spec)
        {
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

        Value make_search_locations(ThreadState *thread, const ModuleSpec &spec)
        {
            if(!spec.is_package)
            {
                return Value::None();
            }

            Owned<TValue<List>> locations(thread->make_object_value<List>());
            for(const std::wstring &location: spec.submodule_search_locations)
            {
                locations.extract()->append(
                    interned_string(thread, location).raw_value());
            }
            return locations.raw_value();
        }

        std::wstring parent_name_for_spec(const ModuleSpec &spec)
        {
            return spec.is_package ? spec.name : parent_module_name(spec.name);
        }

        const wchar_t *loader_kind_for_spec(const ModuleSpec &spec)
        {
            switch(spec.kind)
            {
                case ModuleSpecKind::Source:
                    return L"source";
                case ModuleSpecKind::Builtin:
                    return L"builtin";
                case ModuleSpecKind::Namespace:
                    return L"namespace";
            }
            __builtin_unreachable();
        }

        Value loader_path_for_spec(ThreadState *thread, const ModuleSpec &spec)
        {
            if(spec.kind == ModuleSpecKind::Source)
            {
                return interned_string(thread, spec.origin).raw_value();
            }
            return Value::None();
        }

        Owned<TValue<ModuleLoaderObject>>
        make_module_loader(ThreadState *thread, const ModuleSpec &spec)
        {
            Value name = spec.kind == ModuleSpecKind::Builtin
                             ? Value::None()
                             : interned_string(thread, spec.name).raw_value();
            return Owned<TValue<ModuleLoaderObject>>(
                thread->make_object_value<ModuleLoaderObject>(
                    interned_string(thread, loader_kind_for_spec(spec))
                        .raw_value(),
                    name, loader_path_for_spec(thread, spec)));
        }

        Owned<TValue<ModuleSpecObject>>
        make_module_spec_object(ThreadState *thread, const ModuleSpec &spec,
                                Value loader, Value search_locations)
        {
            return Owned<TValue<ModuleSpecObject>>(
                thread->make_object_value<ModuleSpecObject>(
                    interned_string(thread, spec.name).raw_value(), loader,
                    interned_string(thread, spec.origin).raw_value(),
                    search_locations,
                    spec.kind == ModuleSpecKind::Source ? Value::True()
                                                        : Value::False(),
                    interned_string(thread, parent_name_for_spec(spec))
                        .raw_value()));
        }

        Value load_builtin_module(ThreadState *thread, const ModuleSpec &spec,
                                  TValue<String> name)
        {
            assert(spec.kind == ModuleSpecKind::Builtin);
            VirtualMachine *machine = thread->get_machine();
            Value module = Value::not_present();
            if(spec.name == L"sys")
            {
                module = machine->sys_module().raw_value();
            }
            else if(spec.name == L"builtins")
            {
                module = machine->global_builtins_module().raw_value();
            }
            else
            {
                return set_module_not_found(thread, spec.name);
            }

            machine->imported_modules().extract()->set_item(name.raw_value(),
                                                            module);
            return module;
        }

        Owned<TValue<ModuleObject>>
        create_module_from_spec(ThreadState *thread, const ModuleSpec &spec,
                                TValue<String> name)
        {
            const std::wstring package_name =
                spec.is_package ? spec.name : parent_module_name(spec.name);
            Owned<Value> search_locations(make_search_locations(thread, spec));
            Owned<TValue<ModuleLoaderObject>> loader(
                make_module_loader(thread, spec));
            Owned<TValue<ModuleSpecObject>> spec_object(make_module_spec_object(
                thread, spec, loader.raw_value(), search_locations.value()));
            Value file = spec.kind == ModuleSpecKind::Namespace
                             ? Value::not_present()
                             : interned_string(thread, spec.origin).raw_value();
            Owned<TValue<ModuleObject>> module(
                TValue<ModuleObject>::from_oop(thread->make_module_object(
                    name,
                    thread->get_machine()->global_builtins_module().raw_value(),
                    Value::None(),
                    interned_string(thread, package_name).raw_value(),
                    loader.raw_value(), spec_object.raw_value(), file)));
            install_package_import_metadata(thread, module.extract(), spec);
            return module;
        }

        Value module_from_sys_modules_after_exec(ThreadState *thread,
                                                 TValue<String> name)
        {
            Dict *modules = thread->get_machine()->imported_modules().extract();
            if(!modules->contains(name.raw_value()))
            {
                return thread->set_pending_builtin_exception_string(
                    L"ImportError",
                    L"loader removed module from sys.modules during import");
            }

            Value module = modules->get_item(name.raw_value());
            if(module == Value::None())
            {
                return set_module_not_found(thread, string_to_wstring(name));
            }
            return module;
        }

        Value exec_source_module(ThreadState *thread, const ModuleSpec &spec,
                                 TValue<String> name, ModuleObject *module)
        {
            try
            {
                std::optional<std::wstring> source =
                    read_source_text_file(spec.origin);
                if(!source.has_value())
                {
                    remove_imported_module(thread, name);
                    return set_module_load_failed(thread, spec.name);
                }

                CodeObject *code = thread->compile_in_module(
                    source->c_str(), StartRule::File, module,
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

            return module_from_sys_modules_after_exec(thread, name);
        }

        Value load_source_module(ThreadState *thread, const ModuleSpec &spec,
                                 TValue<String> name)
        {
            Dict *modules = thread->get_machine()->imported_modules().extract();
            Owned<TValue<ModuleObject>> module(
                create_module_from_spec(thread, spec, name));
            modules->set_item(name.raw_value(), module.raw_value());
            return exec_source_module(thread, spec, name, module.extract());
        }

        Value load_namespace_module(ThreadState *thread, const ModuleSpec &spec,
                                    TValue<String> name)
        {
            Dict *modules = thread->get_machine()->imported_modules().extract();
            Owned<TValue<ModuleObject>> module(
                create_module_from_spec(thread, spec, name));
            modules->set_item(name.raw_value(), module.raw_value());
            return module.raw_value();
        }

        Value load_from_spec(ThreadState *thread, const ModuleSpec &spec,
                             TValue<String> name)
        {
            switch(spec.kind)
            {
                case ModuleSpecKind::Builtin:
                    return load_builtin_module(thread, spec, name);
                case ModuleSpecKind::Source:
                    return load_source_module(thread, spec, name);
                case ModuleSpecKind::Namespace:
                    return load_namespace_module(thread, spec, name);
            }
            __builtin_unreachable();
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

        Value package_path(ThreadState *thread, Value parent,
                           const std::wstring &full_name,
                           const std::wstring &parent_name)
        {
            if(!can_convert_to<ModuleObject>(parent))
            {
                return set_not_a_package(thread, full_name, parent_name);
            }

            TValue<String> path_name = interned_string(thread, L"__path__");
            Value path =
                parent.get_ptr<ModuleObject>()->get_own_property(path_name);
            if(!can_convert_to<List>(path))
            {
                return set_not_a_package(thread, full_name, parent_name);
            }
            return path;
        }

        Value load_module_from_path(ThreadState *thread,
                                    const std::wstring &full_name,
                                    const std::wstring &leaf_name, List *path)
        {
            TValue<String> name = interned_string(thread, full_name);
            Value cached = get_cached_module(thread, name);
            if(!cached.is_not_present())
            {
                return cached;
            }

            std::optional<ModuleSpec> spec =
                find_module_spec(thread, full_name, leaf_name, path);
            if(!spec.has_value())
            {
                return set_module_not_found(thread, full_name);
            }

            return load_from_spec(thread, *spec, name);
        }
    }  // namespace

    Value import_module_absolute(ThreadState *thread, TValue<String> name)
    {
        std::wstring module_name = string_to_wstring(name);
        if(module_name.empty())
        {
            return set_module_not_found(thread, module_name);
        }
        std::vector<std::wstring> components = split_module_name(module_name);
        for(const std::wstring &component: components)
        {
            if(component.empty())
            {
                return set_module_not_found(thread, module_name);
            }
        }

        List *top_path = sys_path(thread);
        std::wstring current_name = components[0];
        Owned<Value> current(load_module_from_path(thread, current_name,
                                                   components[0], top_path));
        if(current.value().is_exception_marker())
        {
            return current.value();
        }

        for(size_t component_idx = 1; component_idx < components.size();
            ++component_idx)
        {
            std::wstring parent_name = current_name;
            current_name += L".";
            current_name += components[component_idx];
            Owned<Value> path(package_path(thread, current.value(),
                                           current_name, parent_name));
            if(path.value().is_exception_marker())
            {
                return path.value();
            }

            Owned<Value> child(load_module_from_path(
                thread, current_name, components[component_idx],
                path.value().get_ptr<List>()));
            if(child.value().is_exception_marker())
            {
                return child.value();
            }

            bool stored =
                current.value().get_ptr<ModuleObject>()->set_own_property(
                    interned_string(thread, components[component_idx]),
                    child.value());
            assert(stored);
            (void)stored;
            current = child.value();
        }

        return current.value();
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

    Value import_from(ThreadState *thread, Value module, TValue<String> name)
    {
        if(!can_convert_to<ModuleObject>(module))
        {
            return set_cannot_import_name(thread, module, name);
        }
        ModuleObject *module_object = module.get_ptr<ModuleObject>();
        Value imported = module_object->get_own_property(name);
        if(imported.is_not_present())
        {
            Value package_path = module_object->get_own_property(
                interned_string(thread, L"__path__"));
            Value module_name = module_object->get_name_binding();
            if(can_convert_to<List>(package_path) &&
               can_convert_to<String>(module_name))
            {
                std::wstring child_name = string_to_wstring(
                    TValue<String>::from_value_assumed(module_name));
                child_name += L".";
                child_name += string_to_wstring(name);
                Value child = import_module_absolute(
                    thread, interned_string(thread, child_name));
                if(!child.is_exception_marker())
                {
                    return child;
                }
                if(thread->pending_exception_kind() ==
                       PendingExceptionKind::Object &&
                   thread->pending_exception_object()
                           .extract()
                           ->get_shape()
                           ->get_class() ==
                       thread->class_for_builtin_name(L"ModuleNotFoundError"))
                {
                    thread->clear_pending_exception();
                    return set_cannot_import_name(thread, module, name);
                }
                return child;
            }
            return set_cannot_import_name(thread, module, name);
        }
        return imported;
    }

    Value import_star(ThreadState *thread, CodeObject *code_object,
                      Value module)
    {
        if(!can_convert_to<ModuleObject>(module))
        {
            return thread->set_pending_builtin_exception_string(
                L"ImportError",
                L"from-import-* object has no __dict__ and no __all__");
        }

        Owned<TValue<ModuleObject>> source(
            TValue<ModuleObject>::from_value_assumed(module));
        Owned<TValue<ModuleObject>> target(code_object->get_defining_module());
        std::vector<Owned<TValue<String>>> names;
        TValue<String> dunder_all = interned_string(thread, L"__all__");
        Value all = source.extract()->get_own_property(dunder_all);
        if(!all.is_not_present())
        {
            Owned<Value> iterator(thread->call_clovervm_method(
                all, interned_string(thread, L"__iter__")));
            if(iterator.value().is_exception_marker())
            {
                return iterator.value();
            }

            const std::wstring module_name =
                module_name_for_message(source.raw_value());
            while(true)
            {
                Owned<Value> item(thread->call_clovervm_method(
                    iterator.value(), interned_string(thread, L"__next__")));
                if(item.value().is_exception_marker())
                {
                    if(pending_exception_is_stop_iteration(thread))
                    {
                        thread->clear_pending_exception();
                        break;
                    }
                    return item.value();
                }
                if(!can_convert_to<String>(item.value()))
                {
                    return set_bad_all_item(thread, module_name, item.value());
                }
                names.emplace_back(
                    TValue<String>::from_value_assumed(item.value()));
            }

            for(const Owned<TValue<String>> &name: names)
            {
                Owned<Value> imported(
                    import_from(thread, source.raw_value(), name.value()));
                if(imported.value().is_exception_marker())
                {
                    return imported.value();
                }
                if(!target.extract()->set_own_property(name.value(),
                                                       imported.value()))
                {
                    return thread->set_pending_builtin_exception_string(
                        L"TypeError",
                        L"module globals do not allow star import binding");
                }
            }
            return Value::None();
        }

        Owned<TValue<SlotDict>> source_dict(
            thread->make_object_value<SlotDict>(source.extract()));
        for(SlotDict::EntryView entry: *source_dict.extract())
        {
            assert(can_convert_to<String>(entry.key));
            TValue<String> name = TValue<String>::from_value_assumed(entry.key);
            if(!string_starts_with_underscore(name))
            {
                names.emplace_back(name);
            }
        }

        for(const Owned<TValue<String>> &name: names)
        {
            Value value = source.extract()->get_own_property(name.value());
            if(value.is_not_present())
            {
                continue;
            }
            if(!target.extract()->set_own_property(name.value(), value))
            {
                return thread->set_pending_builtin_exception_string(
                    L"TypeError",
                    L"module globals do not allow star import binding");
            }
        }
        return Value::None();
    }

}  // namespace cl
