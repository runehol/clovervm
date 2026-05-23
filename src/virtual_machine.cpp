#include "virtual_machine.h"
#include "bool.h"
#include "build_config.h"
#include "clover_entry.h"
#include "code_object.h"
#include "codegen.h"
#include "dict.h"
#include "exception_object.h"
#include "exception_propagation.h"
#include "float.h"
#include "function.h"
#include "heap_reclamation.h"
#include "import_system.h"
#include "instance.h"
#include "int.h"
#include "list.h"
#include "list_iterator.h"
#include "module_object.h"
#include "native_function.h"
#include "none_type.h"
#include "parser.h"
#include "range_iterator.h"
#include "shape.h"
#include "slot_dict.h"
#include "thread_state.h"
#include "tuple.h"
#include "tuple_iterator.h"
#include "typed_value.h"
#include <cassert>
#include <cmath>
#include <cwchar>
#include <initializer_list>
#include <stdexcept>

#include "builtins.inc"

namespace cl
{
    static Value make_class_tuple(std::initializer_list<ClassObject *> classes)
    {
        Tuple *tuple = make_object_raw<Tuple>(classes.size());
        size_t idx = 0;
        for(ClassObject *cls: classes)
        {
            assert(cls != nullptr);
            tuple->initialize_item_unchecked(idx++, Value::from_oop(cls));
        }
        return Value::from_oop(tuple);
    }

    [[nodiscard]] static Value require_range_integer_arg(Value arg,
                                                         Value &arg_out)
    {
        if(!arg.is_integer())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"range() arguments must be integers");
        }
        arg_out = arg;
        return Value::None();
    }

    static Value builtin_range(Value start_arg, Value end_arg, Value step_arg)
    {
        Value start;
        Value stop;
        Value step;
        if(end_arg == Value::None())
        {
            start = Value::from_smi(0);
            stop = start_arg;
        }
        else
        {
            start = start_arg;
            stop = end_arg;
        }
        if(step_arg == Value::None())
        {
            step = Value::from_smi(1);
        }
        else
        {
            step = step_arg;
        }

        CL_PROPAGATE_EXCEPTION(require_range_integer_arg(start, start));
        CL_PROPAGATE_EXCEPTION(require_range_integer_arg(stop, stop));
        CL_PROPAGATE_EXCEPTION(require_range_integer_arg(step, step));
        if(step.get_smi() == 0)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"range() arg 3 must not be zero");
        }

        return make_object_value<RangeIterator>(
                   TValue<SMI>::from_value_assumed(start),
                   TValue<SMI>::from_value_assumed(stop),
                   TValue<SMI>::from_value_assumed(step))
            .raw_value();
    }

    static Value builtin_sqrt(Value arg)
    {
        double value;
        if(arg.is_smi())
        {
            value = static_cast<double>(arg.get_smi());
        }
        else if(can_convert_to<Float>(arg))
        {
            value = arg.get_ptr<Float>()->value;
        }
        else
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"sqrt() argument must be int or float");
        }

        if(value < 0.0)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"math domain error");
        }

        return active_thread()
            ->make_object_value<Float>(std::sqrt(value))
            .raw_value();
    }

    static Value builtin_import(Value name, Value globals, Value locals,
                                Value fromlist, Value level)
    {
        (void)locals;
        (void)fromlist;

        if(!can_convert_to<String>(name))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"__import__ name must be str");
        }

        if(!level.is_smi())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"__import__ level must be int");
        }

        auto import_error = [](const wchar_t *message) {
            return active_thread()->set_pending_builtin_exception_string(
                L"ImportError", message);
        };

        auto globals_item = [](Value globals_arg, TValue<String> key) {
            if(can_convert_to<SlotDict>(globals_arg))
            {
                return globals_arg.get_ptr<SlotDict>()->get_item(
                    key.raw_value());
            }
            if(can_convert_to<Dict>(globals_arg))
            {
                Value result =
                    globals_arg.get_ptr<Dict>()->get_item(key.raw_value());
                if(result.is_exception_marker())
                {
                    active_thread()->clear_pending_exception();
                    return Value::not_present();
                }
                return result;
            }
            return Value::not_present();
        };

        auto resolve_relative_name =
            [&](TValue<String> relative_name,
                int64_t relative_level) -> Optional<TValue<String>> {
            if(relative_level == 0)
            {
                return Optional<TValue<String>>::some(relative_name);
            }

            TValue<String> package_key =
                active_thread()
                    ->get_machine()
                    ->get_or_create_interned_string_value(L"__package__");
            Value package_value = globals_item(globals, package_key);
            if(!can_convert_to<String>(package_value))
            {
                import_error(
                    L"attempted relative import with no known parent package");
                return Optional<TValue<String>>::none();
            }

            std::wstring package = string_as_wchar_t(
                TValue<String>::from_value_assumed(package_value));
            if(package.empty())
            {
                import_error(
                    L"attempted relative import with no known parent package");
                return Optional<TValue<String>>::none();
            }

            for(int64_t level_idx = 1; level_idx < relative_level; ++level_idx)
            {
                size_t dot = package.rfind(L'.');
                if(dot == std::wstring::npos)
                {
                    import_error(
                        L"attempted relative import beyond top-level package");
                    return Optional<TValue<String>>::none();
                }
                package.resize(dot);
            }

            std::wstring tail = string_as_wchar_t(relative_name);
            std::wstring absolute_name = package;
            if(!tail.empty())
            {
                absolute_name += L".";
                absolute_name += tail;
            }
            return Optional<TValue<String>>::some(
                active_thread()
                    ->get_machine()
                    ->get_or_create_interned_string_value(absolute_name));
        };

        if(level.get_smi() < 0)
        {
            return import_error(L"level must be >= 0");
        }

        Optional<TValue<String>> maybe_module_name = resolve_relative_name(
            TValue<String>::from_value_assumed(name), level.get_smi());
        if(!maybe_module_name.has_value())
        {
            return Value::exception_marker();
        }

        TValue<String> module_name = maybe_module_name.value();
        Value imported = import_module_absolute(active_thread(), module_name);
        bool wants_leaf_module = !fromlist.is_none();
        if(can_convert_to<Tuple>(fromlist))
        {
            wants_leaf_module = !fromlist.get_ptr<Tuple>()->empty();
        }
        else if(can_convert_to<List>(fromlist))
        {
            wants_leaf_module = fromlist.get_ptr<List>()->size() != 0;
        }
        if(imported.is_exception_marker() || wants_leaf_module)
        {
            return imported;
        }

        std::wstring full_name = string_as_wchar_t(module_name);
        size_t dot = full_name.find(L'.');
        if(dot == std::wstring::npos)
        {
            return imported;
        }

        TValue<String> top_name =
            active_thread()->get_machine()->get_or_create_interned_string_value(
                full_name.substr(0, dot));
        return active_thread()
            ->get_machine()
            ->imported_modules()
            .extract()
            ->get_item(top_name.raw_value());
    }

    VirtualMachine::VirtualMachine()
        : refcounted_global_heap(GlobalHeap::refcounted_heap()),
          interned_global_heap(GlobalHeap::interned_heap()),
          interned_strings(&interned_global_heap), range_builtin(Value::None())
    {
        // make the main thread
        ThreadState *default_thread = make_new_thread();
        ThreadState::ActivationScope activation_scope(default_thread);
        try
        {
            initialize_builtins();
            default_thread->switch_to_new_heap_slabs();
        }
        catch(...)
        {
            range_builtin = Value::None();
            global_builtins_module_ = nullptr;
            sys_module_ = nullptr;
            imported_modules_ = nullptr;
            throw;
        }
    }

    VirtualMachine::~VirtualMachine()
    {
        if(!threads.empty())
        {
            ThreadState::ActivationScope activation_scope(threads[0].get());
            range_builtin = Value::None();
            global_builtins_module_ = nullptr;
            sys_module_ = nullptr;
            imported_modules_ = nullptr;
        }
    }

    ThreadState *VirtualMachine::make_new_thread()
    {
        return threads.emplace_back(std::make_unique<ThreadState>(this)).get();
    }

    void VirtualMachine::run_heap_reclamation()
    {
        cl::run_heap_reclamation(threads);
    }

    void VirtualMachine::complete_safepoint()
    {
        run_heap_reclamation();
        clear_safepoint_request();
        if(fire_every_safepoint_for_testing())
        {
            request_safepoint();
        }
    }

    void VirtualMachine::write_stdout(TValue<String> value)
    {
        String *string = value.extract();
        size_t count = static_cast<size_t>(string->count.extract());
        for(size_t idx = 0; idx < count; ++idx)
        {
            std::fputwc(string->data[idx], stdout_file_);
        }
    }

    TValue<ModuleObject> VirtualMachine::sys_module() const
    {
        assert(sys_module_ != nullptr);
        return TValue<ModuleObject>::from_oop(sys_module_);
    }

    TValue<Dict> VirtualMachine::imported_modules() const
    {
        assert(imported_modules_ != nullptr);
        return TValue<Dict>::from_oop(imported_modules_);
    }

    CodeObject *VirtualMachine::clover_function_entry_adapter(uint32_t n_args)
    {
        if(n_args >= clover_function_entry_adapters.size())
        {
            throw std::runtime_error(
                "unsupported Clover function entry adapter arity");
        }
        CodeObject *&adapter = clover_function_entry_adapters[n_args];
        if(adapter == nullptr)
        {
            adapter =
                make_clover_function_entry_adapter_code_object(this, n_args);
        }
        return adapter;
    }

    void VirtualMachine::register_builtin_class(
        const BuiltinClassDefinition &definition)
    {
        assert(definition.cls != nullptr);
        builtin_classes.push_back(definition.cls);
        for(size_t idx = 0; idx < definition.native_layout_id_count; ++idx)
        {
            NativeLayoutId native_layout_id = definition.native_layout_ids[idx];
            assert(native_layout_id != NativeLayoutId::Invalid);
            assert(static_cast<size_t>(native_layout_id) < NativeLayoutCount);
            assert(class_for_native_layouts[static_cast<size_t>(
                       native_layout_id)] == nullptr);
            class_for_native_layouts[static_cast<size_t>(native_layout_id)] =
                definition.cls;
            for(const std::unique_ptr<ThreadState> &thread: threads)
            {
                thread->cache_class_for_native_layout(native_layout_id,
                                                      definition.cls);
            }
        }
    }

    void VirtualMachine::install_bootstrap_string_class()
    {
        str_class_ = class_for_native_layout(NativeLayoutId::String);
        assert(str_class_ != nullptr);
        str_instance_root_shape_ = str_class_->get_instance_root_shape();
        assert(str_instance_root_shape_ != nullptr);

        ClassObject *cls = str_class_;
        interned_strings.for_each_raw([cls](String *string) {
            if(!string->Object::is_class_bootstrapped())
            {
                string->install_bootstrap_class(cls);
            }
        });
    }

    static void install_bootstrap_tuple_class_on_value(Value value,
                                                       ClassObject *tuple_class)
    {
        assert(can_convert_to<Tuple>(value));
        Object *object = value.get_ptr<Object>();
        if(!object->is_class_bootstrapped())
        {
            object->install_bootstrap_class(tuple_class);
        }
        if(object->get_shape() == nullptr)
        {
            object->set_shape(tuple_class->get_instance_root_shape());
        }
    }

    void VirtualMachine::install_bootstrap_tuple_class()
    {
        ClassObject *tuple = tuple_class();
        assert(tuple != nullptr);

        for(ClassObject *cls: builtin_classes)
        {
            install_bootstrap_tuple_class_on_value(
                cls->read_storage_location(
                    StorageLocation{ClassObject::class_metadata_slot_bases,
                                    StorageKind::Inline}),
                tuple);
            install_bootstrap_tuple_class_on_value(
                cls->read_storage_location(StorageLocation{
                    ClassObject::class_metadata_slot_mro, StorageKind::Inline}),
                tuple);
        }
    }

    void VirtualMachine::initialize_builtin_types()
    {
        dunder_class_name_ = get_or_create_interned_string_raw(L"__class__");
        BuiltinClassDefinition type_definition = make_type_class(this);
        type_class_ = type_definition.cls;
        register_builtin_class(type_definition);

        register_builtin_class(make_object_class(this));
        register_builtin_class(make_str_class(this));
        install_bootstrap_string_class();
        register_builtin_class(make_tuple_class(this));

        ClassObject *type = type_class();
        assert(type != nullptr);
        ClassObject *object = object_class();
        assert(object != nullptr);
        ClassObject *str = str_class();
        assert(str != nullptr);
        ClassObject *tuple = tuple_class();
        assert(tuple != nullptr);

        object->install_bootstrap_inheritance(make_class_tuple({}),
                                              make_class_tuple({object}));
        type->install_bootstrap_inheritance(make_class_tuple({object}),
                                            make_class_tuple({type, object}));
        str->install_bootstrap_inheritance(make_class_tuple({object}),
                                           make_class_tuple({str, object}));
        tuple->install_bootstrap_inheritance(make_class_tuple({object}),
                                             make_class_tuple({tuple, object}));

        install_bootstrap_tuple_class();
        BuiltinClassDefinition int_definition = make_int_class(this);
        int_class_ = int_definition.cls;
        register_builtin_class(int_definition);
        DescriptorFlags inline_class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot) |
            descriptor_flag(DescriptorFlag::SpecialRead) |
            descriptor_flag(DescriptorFlag::SpecialMutate);
        smi_shape_ = Shape::make_immortal_root_with_single_descriptor(
            this, TValue<ClassObject>::from_oop(int_class_),
            dunder_class_name(),
            DescriptorInfo::make(StorageLocation::not_found(),
                                 inline_class_flags,
                                 DescriptorSpecialKind::ShapeClass),
            0, 0, fixed_attribute_shape_flags());
        BuiltinClassDefinition bool_definition =
            make_bool_class(this, int_class_);
        bool_class_ = bool_definition.cls;
        register_builtin_class(bool_definition);
        bool_shape_ = Shape::make_immortal_root_with_single_descriptor(
            this, TValue<ClassObject>::from_oop(bool_class_),
            dunder_class_name(),
            DescriptorInfo::make(StorageLocation::not_found(),
                                 inline_class_flags,
                                 DescriptorSpecialKind::ShapeClass),
            0, 0, fixed_attribute_shape_flags());
        BuiltinClassDefinition none_type_definition =
            make_none_type_class(this);
        none_type_class_ = none_type_definition.cls;
        register_builtin_class(none_type_definition);
        none_shape_ = Shape::make_immortal_root_with_single_descriptor(
            this, TValue<ClassObject>::from_oop(none_type_class_),
            dunder_class_name(),
            DescriptorInfo::make(StorageLocation::not_found(),
                                 inline_class_flags,
                                 DescriptorSpecialKind::ShapeClass),
            0, 0, fixed_attribute_shape_flags());
        register_builtin_class(make_list_class(this));
        register_builtin_class(make_dict_class(this));
        register_builtin_class(make_slotdict_class(this));
        register_builtin_class(make_float_class(this));
        register_builtin_class(make_module_class(this));
        TValue<String> builtins_name =
            get_or_create_interned_string_value(L"builtins");
        TValue<String> empty_package = get_or_create_interned_string_value(L"");
        ModuleObject *builtins_module = make_immortal_object_raw<ModuleObject>(
            builtins_name, Value::not_present(), Value::None(),
            empty_package.raw_value(), Value::None(), Value::None(),
            Value::not_present());
        global_builtins_module_ = builtins_module;
        register_builtin_class(make_function_class(this));
        register_builtin_class(make_code_object_class(this));
        register_builtin_class(make_range_iterator_class(this));
        register_builtin_class(make_tuple_iterator_class(this));
        register_builtin_class(make_list_iterator_class(this));
        BuiltinClassDefinition base_exception_definition =
            make_base_exception_class(this);
        ClassObject *base_exception = base_exception_definition.cls;
        register_builtin_class(base_exception_definition);
        BuiltinClassDefinition exception_definition =
            make_exception_class(this, base_exception);
        ClassObject *exception = exception_definition.cls;
        register_builtin_class(exception_definition);
        BuiltinClassDefinition import_error_definition =
            make_exception_subclass(this, L"ImportError", exception);
        ClassObject *import_error = import_error_definition.cls;
        register_builtin_class(import_error_definition);
        register_builtin_class(make_exception_subclass(
            this, L"ModuleNotFoundError", import_error));
        register_builtin_class(
            make_exception_subclass(this, L"NameError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"TypeError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"ValueError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"ZeroDivisionError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"RuntimeError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"UnimplementedError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"AssertionError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"AttributeError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"IndexError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"KeyError", exception));
        register_builtin_class(make_stop_iteration_class(this, exception));

        install_object_class_methods(this);
        install_str_class_methods(this);
        install_int_class_methods(this);
        install_bool_class_methods(this);
        install_none_type_class_methods(this);
        install_list_class_methods(this);
        install_tuple_class_methods(this);
        install_dict_class_methods(this);
        install_slotdict_class_methods(this);
        install_float_class_methods(this);
        install_module_class_methods(this);
    }

    void VirtualMachine::initialize_module_bootstrap()
    {
        TValue<ModuleObject> builtins_module = global_builtins_module();

        TValue<String> sys_name = get_or_create_interned_string_value(L"sys");
        TValue<String> empty_package = get_or_create_interned_string_value(L"");
        sys_module_ = make_immortal_object_raw<ModuleObject>(
            sys_name, builtins_module.raw_value(), Value::None(),
            empty_package.raw_value(), Value::None(), Value::None(),
            Value::not_present());

        imported_modules_ = make_immortal_object_raw<Dict>();

        TValue<String> dot = get_or_create_interned_string_value(L".");
        TValue<String> stdlib_dir =
            get_or_create_interned_string_value(CL_STDLIB_DIR);
        List *path = make_immortal_object_raw<List>(2);
        path->set_item_unchecked(0, dot.raw_value());
        path->set_item_unchecked(1, stdlib_dir.raw_value());

        TValue<String> modules_name =
            get_or_create_interned_string_value(L"modules");
        TValue<String> path_name = get_or_create_interned_string_value(L"path");
        bool installed_modules = sys_module_->set_own_property(
            modules_name, Value::from_oop(imported_modules_));
        bool installed_path =
            sys_module_->set_own_property(path_name, Value::from_oop(path));
        assert(installed_modules);
        assert(installed_path);
        (void)installed_modules;
        (void)installed_path;

        TValue<String> builtins_name =
            get_or_create_interned_string_value(L"builtins");
        imported_modules_->set_item(sys_name.raw_value(),
                                    Value::from_oop(sys_module_));
        imported_modules_->set_item(builtins_name.raw_value(),
                                    builtins_module.raw_value());
    }

    void VirtualMachine::initialize_builtins()
    {
        initialize_builtin_types();
        get_default_thread()->refresh_class_for_native_layout_cache();

        initialize_module_bootstrap();

        ModuleObject *builtins_module = global_builtins_module().extract();

        auto install_builtin_binding = [&](TValue<String> name, Value value) {
            bool installed = builtins_module->set_own_property(name, value);
            assert(installed);
            (void)installed;
        };

        for(ClassObject *cls: builtin_classes)
        {
            if(cls == code_class() || cls == slotdict_class())
            {
                continue;
            }
            install_builtin_binding(cls->get_name(), Value::from_oop(cls));
        }

        install_builtin_binding(get_or_create_interned_string_value(L"True"),
                                Value::True());
        install_builtin_binding(get_or_create_interned_string_value(L"False"),
                                Value::False());
        install_builtin_binding(get_or_create_interned_string_value(L"None"),
                                Value::None());

        TValue<String> range_name =
            get_or_create_interned_string_value(L"range");
        TValue<Tuple> range_defaults = make_object_value<Tuple>(2);
        range_defaults.extract()->initialize_item_unchecked(0, Value::None());
        range_defaults.extract()->initialize_item_unchecked(1, Value::None());
        range_builtin =
            make_native_function(this, builtin_range,
                                 Optional<TValue<Tuple>>::some(range_defaults))
                .raw_value();
        install_builtin_binding(range_name, range_builtin);

        TValue<String> sqrt_name = get_or_create_interned_string_value(L"sqrt");
        install_builtin_binding(
            sqrt_name, make_native_function(this, builtin_sqrt).raw_value());

        TValue<String> import_name =
            get_or_create_interned_string_value(L"__import__");
        TValue<Tuple> import_defaults = make_object_value<Tuple>(4);
        import_defaults.extract()->initialize_item_unchecked(0, Value::None());
        import_defaults.extract()->initialize_item_unchecked(1, Value::None());
        import_defaults.extract()->initialize_item_unchecked(
            2, make_object_value<Tuple>(0).raw_value());
        import_defaults.extract()->initialize_item_unchecked(
            3, Value::from_smi(0));
        install_builtin_binding(
            import_name,
            make_native_function(this, builtin_import,
                                 Optional<TValue<Tuple>>::some(import_defaults))
                .raw_value());

        ThreadState *thread = get_default_thread();
        CodeObject *builtins_code = thread->compile_in_module(
            trusted_builtin_source, StartRule::File, builtins_module,
            LanguageMode::TrustedCloverExtensions);
        Value result = thread->run_clovervm_code_object(builtins_code);
        if(result.is_exception_marker())
        {
            throw std::runtime_error(
                "failed to initialize trusted builtins.py");
        }
    }

}  // namespace cl
