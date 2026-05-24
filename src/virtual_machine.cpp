#include "virtual_machine.h"
#include "bool.h"
#include "build_config.h"
#include "builtins.h"
#include "clover_entry.h"
#include "code_object.h"
#include "codegen.h"
#include "dict.h"
#include "dict_view.h"
#include "ellipsis_type.h"
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
#include "module_loader_object.h"
#include "module_object.h"
#include "module_spec_object.h"
#include "native_function.h"
#include "native_module_loader_internal.h"
#include "none_type.h"
#include "not_implemented_type.h"
#include "parser.h"
#include "range_iterator.h"
#include "shape.h"
#include "slot_dict.h"
#include "thread_state.h"
#include "tuple.h"
#include "tuple_iterator.h"
#include "typed_value.h"
#include <cassert>
#include <cstdint>
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

    static void install_module_value(VirtualMachine *vm, ModuleObject *module,
                                     const wchar_t *name, Value value)
    {
        bool installed = module->set_own_property(
            vm->get_or_create_interned_string_value(name), value);
        assert(installed);
        (void)installed;
    }

    static Value make_string_value(VirtualMachine *vm, const wchar_t *value)
    {
        return vm->get_or_create_interned_string_value(value).raw_value();
    }

    static Value
    make_string_tuple(VirtualMachine *vm,
                      std::initializer_list<const wchar_t *> values)
    {
        Tuple *tuple = vm->make_immortal_object_raw<Tuple>(values.size());
        size_t idx = 0;
        for(const wchar_t *value: values)
        {
            tuple->initialize_item_unchecked(idx++,
                                             make_string_value(vm, value));
        }
        return Value::from_oop(tuple);
    }

    static Value make_string_list(VirtualMachine *vm,
                                  std::initializer_list<const wchar_t *> values)
    {
        List *list = vm->make_immortal_object_raw<List>();
        for(const wchar_t *value: values)
        {
            list->append(make_string_value(vm, value));
        }
        return Value::from_oop(list);
    }

    static Value make_version_info(VirtualMachine *vm)
    {
        Tuple *version_info = vm->make_immortal_object_raw<Tuple>(5);
        version_info->initialize_item_unchecked(0, Value::from_smi(0));
        version_info->initialize_item_unchecked(1, Value::from_smi(0));
        version_info->initialize_item_unchecked(2, Value::from_smi(0));
        version_info->initialize_item_unchecked(
            3, make_string_value(vm, L"alpha"));
        version_info->initialize_item_unchecked(4, Value::from_smi(0));
        return Value::from_oop(version_info);
    }

    static const wchar_t *sys_platform_name()
    {
#if defined(__APPLE__)
        return L"darwin";
#elif defined(__linux__)
        return L"linux";
#elif defined(_WIN32)
        return L"win32";
#else
        return L"unknown";
#endif
    }

    static const wchar_t *sys_byteorder_name()
    {
        uint16_t value = 1;
        return *reinterpret_cast<unsigned char *>(&value) == 1 ? L"little"
                                                               : L"big";
    }

    static void install_sys_static_attributes(VirtualMachine *vm,
                                              ModuleObject *sys_module)
    {
        install_module_value(vm, sys_module, L"argv",
                             make_string_list(vm, {L""}));
        install_module_value(vm, sys_module, L"orig_argv",
                             make_string_list(vm, {L""}));
        install_module_value(vm, sys_module, L"warnoptions",
                             make_string_list(vm, {}));
        install_module_value(vm, sys_module, L"builtin_module_names",
                             make_string_tuple(vm, {L"builtins", L"sys"}));
        install_module_value(vm, sys_module, L"byteorder",
                             make_string_value(vm, sys_byteorder_name()));
        install_module_value(vm, sys_module, L"copyright",
                             make_string_value(vm, L"Copyright CloverVM"));
        install_module_value(vm, sys_module, L"dont_write_bytecode",
                             Value::True());
        install_module_value(vm, sys_module, L"hexversion", Value::from_smi(0));
        install_module_value(vm, sys_module, L"maxsize",
                             Value::from_smi(value_smi_max));
        install_module_value(vm, sys_module, L"maxunicode",
                             Value::from_smi(0x10ffff));
        install_module_value(vm, sys_module, L"platform",
                             make_string_value(vm, sys_platform_name()));
        install_module_value(vm, sys_module, L"prefix",
                             make_string_value(vm, L""));
        install_module_value(vm, sys_module, L"exec_prefix",
                             make_string_value(vm, L""));
        install_module_value(vm, sys_module, L"base_prefix",
                             make_string_value(vm, L""));
        install_module_value(vm, sys_module, L"base_exec_prefix",
                             make_string_value(vm, L""));
        install_module_value(vm, sys_module, L"version",
                             make_string_value(vm, CL_SYS_VERSION_W));
        install_module_value(vm, sys_module, L"version_info",
                             make_version_info(vm));
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

    static Value builtin_range(ThreadState *thread, Value start_arg,
                               Value end_arg, Value step_arg)
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

    static Value builtin_import(ThreadState *thread, Value name, Value globals,
                                Value locals, Value fromlist, Value level)
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
          interned_strings(&interned_global_heap), range_builtin(Value::None()),
          native_library_handles_(std::make_unique<NativeLibraryHandleCache>())
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

    NativeLibraryHandleCache &VirtualMachine::native_library_handle_cache()
    {
        return *native_library_handles_;
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

    void VirtualMachine::install_native_layout_mappings(
        const BuiltinClassDefinition &definition)
    {
        assert(definition.cls != nullptr);
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

    void VirtualMachine::install_bootstrap_tuple_class(
        const std::vector<BuiltinClassDefinition> &builtin_classes)
    {
        ClassObject *tuple = tuple_class();
        assert(tuple != nullptr);

        for(const BuiltinClassDefinition &definition: builtin_classes)
        {
            ClassObject *cls = definition.cls;
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

    std::vector<BuiltinClassDefinition>
    VirtualMachine::initialize_builtin_types()
    {
        std::vector<BuiltinClassDefinition> builtin_classes;
        auto register_builtin_class = [&](BuiltinClassDefinition definition) {
            install_native_layout_mappings(definition);
            builtin_classes.push_back(definition);
        };

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

        install_bootstrap_tuple_class(builtin_classes);
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
        BuiltinClassDefinition not_implemented_type_definition =
            make_not_implemented_type_class(this);
        not_implemented_type_class_ = not_implemented_type_definition.cls;
        register_builtin_class(not_implemented_type_definition);
        not_implemented_shape_ =
            Shape::make_immortal_root_with_single_descriptor(
                this,
                TValue<ClassObject>::from_oop(not_implemented_type_class_),
                dunder_class_name(),
                DescriptorInfo::make(StorageLocation::not_found(),
                                     inline_class_flags,
                                     DescriptorSpecialKind::ShapeClass),
                0, 0, fixed_attribute_shape_flags());
        BuiltinClassDefinition ellipsis_type_definition =
            make_ellipsis_type_class(this);
        ellipsis_type_class_ = ellipsis_type_definition.cls;
        register_builtin_class(ellipsis_type_definition);
        ellipsis_shape_ = Shape::make_immortal_root_with_single_descriptor(
            this, TValue<ClassObject>::from_oop(ellipsis_type_class_),
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
        register_builtin_class(make_module_loader_class(this));
        register_builtin_class(make_module_spec_class(this));
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
        register_builtin_class(make_dict_keys_view_class(this));
        register_builtin_class(make_dict_values_view_class(this));
        register_builtin_class(make_dict_items_view_class(this));
        register_builtin_class(make_dict_key_iterator_class(this));
        register_builtin_class(make_dict_value_iterator_class(this));
        register_builtin_class(make_dict_item_iterator_class(this));
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
            make_exception_subclass(this, L"OverflowError", exception));
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
        install_not_implemented_type_class_methods(this);
        install_ellipsis_type_class_methods(this);
        install_list_class_methods(this);
        install_tuple_class_methods(this);
        install_dict_class_methods(this);
        install_slotdict_class_methods(this);
        install_float_class_methods(this);
        install_module_class_methods(this);

        return builtin_classes;
    }

    void VirtualMachine::initialize_module_bootstrap()
    {
        TValue<ModuleObject> builtins_module = global_builtins_module();

        TValue<String> sys_name = get_or_create_interned_string_value(L"sys");
        TValue<String> builtins_name =
            get_or_create_interned_string_value(L"builtins");
        TValue<String> builtin_kind =
            get_or_create_interned_string_value(L"builtin");
        TValue<String> builtin_origin =
            get_or_create_interned_string_value(L"built-in");
        TValue<String> empty_package = get_or_create_interned_string_value(L"");
        ModuleLoaderObject *builtins_loader =
            make_immortal_object_raw<ModuleLoaderObject>(
                builtin_kind.raw_value(), Value::None(), Value::None());
        ModuleSpecObject *builtins_spec =
            make_immortal_object_raw<ModuleSpecObject>(
                builtins_name.raw_value(), Value::from_oop(builtins_loader),
                builtin_origin.raw_value(), Value::None(), Value::False(),
                empty_package.raw_value());
        bool installed_builtins_loader =
            builtins_module.extract()->set_own_property(
                get_or_create_interned_string_value(L"__loader__"),
                Value::from_oop(builtins_loader));
        bool installed_builtins_spec =
            builtins_module.extract()->set_own_property(
                get_or_create_interned_string_value(L"__spec__"),
                Value::from_oop(builtins_spec));
        assert(installed_builtins_loader);
        assert(installed_builtins_spec);
        (void)installed_builtins_loader;
        (void)installed_builtins_spec;

        ModuleLoaderObject *sys_loader =
            make_immortal_object_raw<ModuleLoaderObject>(
                builtin_kind.raw_value(), Value::None(), Value::None());
        ModuleSpecObject *sys_spec = make_immortal_object_raw<ModuleSpecObject>(
            sys_name.raw_value(), Value::from_oop(sys_loader),
            builtin_origin.raw_value(), Value::None(), Value::False(),
            empty_package.raw_value());
        sys_module_ = make_immortal_object_raw<ModuleObject>(
            sys_name, builtins_module.raw_value(), Value::None(),
            empty_package.raw_value(), Value::from_oop(sys_loader),
            Value::from_oop(sys_spec), Value::not_present());

        imported_modules_ = make_immortal_object_raw<Dict>();

        TValue<String> current_directory =
            get_or_create_interned_string_value(L"");
        TValue<String> build_stdlib_dir =
            get_or_create_interned_string_value(CL_BUILD_STDLIB_DIR);
        TValue<String> stdlib_dir =
            get_or_create_interned_string_value(CL_STDLIB_DIR);
        List *path = make_immortal_object_raw<List>(3);
        path->set_item_unchecked(0, current_directory.raw_value());
        path->set_item_unchecked(1, build_stdlib_dir.raw_value());
        path->set_item_unchecked(2, stdlib_dir.raw_value());

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

        install_sys_static_attributes(this, sys_module_);

        imported_modules_->set_item(sys_name.raw_value(),
                                    Value::from_oop(sys_module_));
        imported_modules_->set_item(builtins_name.raw_value(),
                                    builtins_module.raw_value());
    }

    void VirtualMachine::initialize_builtins()
    {
        std::vector<BuiltinClassDefinition> builtin_classes =
            initialize_builtin_types();
        get_default_thread()->refresh_class_for_native_layout_cache();

        initialize_module_bootstrap();

        ModuleObject *builtins_module = global_builtins_module().extract();

        auto install_builtin_binding = [&](TValue<String> name, Value value) {
            bool installed = builtins_module->set_own_property(name, value);
            assert(installed);
            (void)installed;
        };

        for(const BuiltinClassDefinition &definition: builtin_classes)
        {
            if(definition.builtins_visibility != BuiltinsVisibility::Public)
            {
                continue;
            }
            install_builtin_binding(definition.cls->get_name(),
                                    Value::from_oop(definition.cls));
        }

        install_builtin_binding(get_or_create_interned_string_value(L"True"),
                                Value::True());
        install_builtin_binding(get_or_create_interned_string_value(L"False"),
                                Value::False());
        install_builtin_binding(get_or_create_interned_string_value(L"None"),
                                Value::None());
        install_builtin_binding(
            get_or_create_interned_string_value(L"NotImplemented"),
            Value::NotImplemented());
        install_builtin_binding(
            get_or_create_interned_string_value(L"Ellipsis"),
            Value::Ellipsis());

        TValue<String> range_name =
            get_or_create_interned_string_value(L"range");
        TValue<Tuple> range_defaults = make_object_value<Tuple>(2);
        range_defaults.extract()->initialize_item_unchecked(0, Value::None());
        range_defaults.extract()->initialize_item_unchecked(1, Value::None());
        range_builtin = make_intrinsic_function(
                            this, builtin_range,
                            Optional<TValue<Tuple>>::some(range_defaults))
                            .raw_value();
        install_builtin_binding(range_name, range_builtin);
        install_builtin_function_bindings(this);

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
            import_name, make_intrinsic_function(
                             this, builtin_import,
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

        CodeObject *sys_code = thread->compile_in_module(
            trusted_sys_source, StartRule::File, sys_module().extract(),
            LanguageMode::TrustedCloverExtensions);
        result = thread->run_clovervm_code_object(sys_code);
        if(result.is_exception_marker())
        {
            throw std::runtime_error("failed to initialize trusted sys.py");
        }
    }

}  // namespace cl
