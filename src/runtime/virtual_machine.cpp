#include "runtime/virtual_machine.h"

#include "api/clover_entry.h"
#include "bootstrap/builtins.h"
#include "build_config.h"
#include "builtin_types/bool.h"
#include "builtin_types/bytes.h"
#include "builtin_types/dict.h"
#include "builtin_types/dict_view.h"
#include "builtin_types/ellipsis_type.h"
#include "builtin_types/enumerate.h"
#include "builtin_types/float.h"
#include "builtin_types/int.h"
#include "builtin_types/list.h"
#include "builtin_types/list_iterator.h"
#include "builtin_types/module_loader_object.h"
#include "builtin_types/module_object.h"
#include "builtin_types/module_spec_object.h"
#include "builtin_types/none_type.h"
#include "builtin_types/not_implemented_type.h"
#include "builtin_types/range_iterator.h"
#include "builtin_types/slice.h"
#include "builtin_types/string_iterator.h"
#include "builtin_types/tuple.h"
#include "builtin_types/tuple_iterator.h"
#include "builtin_types/unicode.h"
#include "builtin_types/zip.h"
#include "bytecode/code_object.h"
#include "compiler/codegen.h"
#include "compiler/parser.h"
#include "import_system/import_system.h"
#include "import_system/module_global.h"
#include "import_system/native_extension_loader_internal.h"
#include "jit/code_cache.h"
#include "memory/heap_reclamation.h"
#include "object_model/function.h"
#include "object_model/instance.h"
#include "object_model/native_function.h"
#include "object_model/shape.h"
#include "object_model/slot_dict.h"
#include "object_model/typed_value.h"
#include "runtime/exception_object.h"
#include "runtime/exception_propagation.h"
#include "runtime/fatal.h"
#include "runtime/operator_walk.h"
#include "runtime/os_error.h"
#include "runtime/protocol_helper_functions.h"
#include "runtime/thread_state.h"
#include <bit>
#include <cassert>
#include <cstdint>
#include <cwchar>
#include <initializer_list>
#include <string>

#include "builtins.inc"

namespace cl
{
    void VirtualMachine::register_trusted_handler_metadata(
        TrustedHandlerTarget target, TrustedHandlerMetadata metadata)
    {
        std::lock_guard<std::mutex> lock(trusted_handler_registry_mutex_);
        auto [entry, inserted] =
            trusted_handler_registry_.emplace(target, metadata);
        if(!inserted && entry->second != metadata)
        {
            fatal("conflicting trusted handler registration");
        }
    }

    std::optional<TrustedHandlerMetadata>
    VirtualMachine::trusted_handler_metadata(TrustedHandlerTarget target) const
    {
        std::lock_guard<std::mutex> lock(trusted_handler_registry_mutex_);
        auto entry = trusted_handler_registry_.find(target);
        if(entry == trusted_handler_registry_.end())
        {
            return std::nullopt;
        }
        return entry->second;
    }

    [[noreturn]] void fatal_bootstrap_python_exception(ThreadState *thread,
                                                       const char *context)
    {
        std::string message = "failed to bootstrap VM";
        if(context != nullptr && context[0] != '\0')
        {
            message += " while ";
            message += context;
        }
        message += ": ";
        message +=
            unicode::encode_utf8(format_pending_python_exception(thread));
        fatal(message);
    }

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
        bool installed = unwrap_bootstrap_expected(
            active_vm(),
            module->set_own_property(
                vm->get_or_create_interned_string_value(name), value),
            "installing module binding");
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
        if constexpr(std::endian::native == std::endian::little)
        {
            return L"little";
        }
        return L"big";
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

    [[nodiscard]] static Expected<TValue<SMI>>
    require_range_integer_arg(Value arg)
    {
        return try_intlike_value_to_smi(arg,
                                        L"range() arguments must be integers");
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

        TValue<SMI> start_smi = CL_TRY(require_range_integer_arg(start));
        TValue<SMI> stop_smi = CL_TRY(require_range_integer_arg(stop));
        TValue<SMI> step_smi = CL_TRY(require_range_integer_arg(step));
        if(step_smi.extract() == 0)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"range() arg 3 must not be zero");
        }

        return make_object_value<RangeIterator>(start_smi, stop_smi, step_smi)
            .raw_value();
    }

    static Value builtin_import(ThreadState *thread, Value name, Value globals,
                                Value locals, Value fromlist, Value level)
    {
        (void)locals;
        (void)fromlist;

        if(!has_native_layout<String>(name))
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
            if(has_native_layout<SlotDict>(globals_arg))
            {
                return globals_arg.get_ptr<SlotDict>()->get_item(
                    key.raw_value());
            }
            if(has_native_layout<Dict>(globals_arg))
            {
                Expected<Value> result =
                    globals_arg.get_ptr<Dict>()->get_item_for_str(
                        active_thread(), key);
                if(result.has_exception())
                {
                    active_thread()->clear_pending_exception();
                    return Value::not_present();
                }
                return result.value();
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
            if(package_value.is_none() || package_value.is_not_present())
            {
                TValue<String> spec_key =
                    active_thread()
                        ->get_machine()
                        ->get_or_create_interned_string_value(L"__spec__");
                Value spec_value = globals_item(globals, spec_key);
                if(has_native_layout<ModuleSpecObject>(spec_value))
                {
                    Value parent =
                        spec_value.get_ptr<ModuleSpecObject>()->parent;
                    if(has_native_layout<String>(parent))
                    {
                        package_value = parent;
                    }
                }
            }
            if(!has_native_layout<String>(package_value))
            {
                if(!package_value.is_none() && !package_value.is_not_present())
                {
                    (void)active_thread()->set_pending_builtin_exception_string(
                        L"TypeError", L"package must be a string");
                    return Optional<TValue<String>>::none();
                }
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
        if(has_native_layout<Tuple>(fromlist))
        {
            wants_leaf_module = !fromlist.get_ptr<Tuple>()->empty();
        }
        else if(has_native_layout<List>(fromlist))
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
        return CL_TRY(active_thread()
                          ->get_machine()
                          ->imported_modules()
                          .extract()
                          ->get_item_for_str(active_thread(), top_name));
    }

    VirtualMachine::VirtualMachine()
        : refcounted_global_heap(GlobalHeap::refcounted_heap()),
          interned_global_heap(GlobalHeap::interned_heap()),
          interned_strings(&interned_global_heap), range_builtin(Value::None()),
          hash_value_helper_function_(Optional<TValue<Function>>::none()),
          test_equal_helper_function_(Optional<TValue<Function>>::none()),
          membership_iter_fallback_function_(
              Optional<TValue<Function>>::none()),
          membership_sequence_fallback_function_(
              Optional<TValue<Function>>::none()),
          native_library_handles_(std::make_unique<NativeLibraryHandleCache>())
    {
        struct BootstrapCleanup
        {
            VirtualMachine *vm;
            bool armed = true;

            ~BootstrapCleanup()
            {
                if(armed)
                {
                    vm->range_builtin = Value::None();
                    vm->os_error_errno_map_ = Optional<TValue<Dict>>::none();
                    vm->hash_value_helper_function_ =
                        Optional<TValue<Function>>::none();
                    vm->test_equal_helper_function_ =
                        Optional<TValue<Function>>::none();
                    vm->enumerate_call_function_ =
                        Optional<TValue<Function>>::none();
                    for(auto &helper: vm->ordering_helper_functions_)
                    {
                        helper = Optional<TValue<Function>>::none();
                    }
                    vm->membership_iter_fallback_function_ =
                        Optional<TValue<Function>>::none();
                    vm->membership_sequence_fallback_function_ =
                        Optional<TValue<Function>>::none();
                    if(vm->exact_dict_general_shape_ != nullptr)
                    {
                        ThreadState::ActivationScope activation_scope(
                            vm->get_default_thread());
                        vm->exact_dict_general_shape_ = nullptr;
                    }
                    vm->global_builtins_module_ = nullptr;
                    vm->builtin_implementation_module_ = nullptr;
                    vm->sys_module_ = nullptr;
                    vm->imported_modules_ = nullptr;
                }
            }
        } bootstrap_cleanup{this};

        // make the main thread
        ThreadState *default_thread = make_new_thread();
        ThreadState::ActivationScope activation_scope(default_thread);
        initialize_builtins();
        default_thread->switch_to_new_heap_slabs();
        bootstrap_cleanup.armed = false;
    }

    VirtualMachine::~VirtualMachine()
    {
        if(!threads.empty())
        {
            ThreadState::ActivationScope activation_scope(threads[0].get());
            range_builtin = Value::None();
            os_error_errno_map_ = Optional<TValue<Dict>>::none();
            hash_value_helper_function_ = Optional<TValue<Function>>::none();
            test_equal_helper_function_ = Optional<TValue<Function>>::none();
            enumerate_call_function_ = Optional<TValue<Function>>::none();
            for(auto &helper: ordering_helper_functions_)
            {
                helper = Optional<TValue<Function>>::none();
            }
            membership_iter_fallback_function_ =
                Optional<TValue<Function>>::none();
            membership_sequence_fallback_function_ =
                Optional<TValue<Function>>::none();
            exact_dict_general_shape_ = nullptr;
            global_builtins_module_ = nullptr;
            builtin_implementation_module_ = nullptr;
            sys_module_ = nullptr;
            imported_modules_ = nullptr;
        }
    }

    NativeLibraryHandleCache &VirtualMachine::native_library_handle_cache()
    {
        return *native_library_handles_;
    }

    jit::CodeCache *VirtualMachine::make_code_cache()
    {
        auto code_cache = std::make_unique<jit::CodeCache>();
        jit::CodeCache *result = code_cache.get();
        jit_code_caches.push_back(std::move(code_cache));
        return result;
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

    Expected<CodeObject *>
    VirtualMachine::clover_function_entry_adapter(uint32_t n_args)
    {
        if(n_args >= clover_function_entry_adapters.size())
        {
            return Expected<CodeObject *>::raise_exception(
                L"SystemError",
                L"unsupported Clover function entry adapter arity");
        }
        CodeObject *&adapter = clover_function_entry_adapters[n_args];
        if(adapter == nullptr)
        {
            adapter = CL_TRY(
                make_clover_function_entry_adapter_code_object(this, n_args));
        }
        return Expected<CodeObject *>::ok(adapter);
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

    void VirtualMachine::install_exact_dict_shapes(ClassObject *cls,
                                                   Shape *string_key_shape,
                                                   Shape *general_shape)
    {
        assert(cls != nullptr);
        assert(string_key_shape != nullptr);
        assert(general_shape != nullptr);
        assert(string_key_shape->get_class() == cls);
        assert(general_shape->get_class() == cls);
        assert(string_key_shape != general_shape);
        exact_dict_string_key_shape_ = string_key_shape;
        exact_dict_general_shape_ = general_shape;
        for(const std::unique_ptr<ThreadState> &thread: threads)
        {
            thread->cache_exact_dict_shapes(string_key_shape, general_shape);
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

    void VirtualMachine::initialize_operator_dispatch_tables()
    {
        assert(str_class_ != nullptr);
        assert(str_instance_root_shape_ != nullptr);

        String *dunder_eq = get_or_create_interned_string_raw(L"__eq__");
        String *dunder_ne = get_or_create_interned_string_raw(L"__ne__");
        String *dunder_lt = get_or_create_interned_string_raw(L"__lt__");
        String *dunder_le = get_or_create_interned_string_raw(L"__le__");
        String *dunder_gt = get_or_create_interned_string_raw(L"__gt__");
        String *dunder_ge = get_or_create_interned_string_raw(L"__ge__");
        String *dunder_add = get_or_create_interned_string_raw(L"__add__");
        String *dunder_radd = get_or_create_interned_string_raw(L"__radd__");
        String *dunder_sub = get_or_create_interned_string_raw(L"__sub__");
        String *dunder_rsub = get_or_create_interned_string_raw(L"__rsub__");
        String *dunder_mul = get_or_create_interned_string_raw(L"__mul__");
        String *dunder_rmul = get_or_create_interned_string_raw(L"__rmul__");
        String *dunder_matmul =
            get_or_create_interned_string_raw(L"__matmul__");
        String *dunder_rmatmul =
            get_or_create_interned_string_raw(L"__rmatmul__");
        String *dunder_pow = get_or_create_interned_string_raw(L"__pow__");
        String *dunder_rpow = get_or_create_interned_string_raw(L"__rpow__");
        String *dunder_truediv =
            get_or_create_interned_string_raw(L"__truediv__");
        String *dunder_rtruediv =
            get_or_create_interned_string_raw(L"__rtruediv__");
        String *dunder_floordiv =
            get_or_create_interned_string_raw(L"__floordiv__");
        String *dunder_rfloordiv =
            get_or_create_interned_string_raw(L"__rfloordiv__");
        String *dunder_mod = get_or_create_interned_string_raw(L"__mod__");
        String *dunder_rmod = get_or_create_interned_string_raw(L"__rmod__");
        String *dunder_lshift =
            get_or_create_interned_string_raw(L"__lshift__");
        String *dunder_rlshift =
            get_or_create_interned_string_raw(L"__rlshift__");
        String *dunder_rshift =
            get_or_create_interned_string_raw(L"__rshift__");
        String *dunder_rrshift =
            get_or_create_interned_string_raw(L"__rrshift__");
        String *dunder_and = get_or_create_interned_string_raw(L"__and__");
        String *dunder_rand = get_or_create_interned_string_raw(L"__rand__");
        String *dunder_xor = get_or_create_interned_string_raw(L"__xor__");
        String *dunder_rxor = get_or_create_interned_string_raw(L"__rxor__");
        String *dunder_or = get_or_create_interned_string_raw(L"__or__");
        String *dunder_ror = get_or_create_interned_string_raw(L"__ror__");
        String *dunder_neg = get_or_create_interned_string_raw(L"__neg__");
        String *dunder_pos = get_or_create_interned_string_raw(L"__pos__");
        String *dunder_invert =
            get_or_create_interned_string_raw(L"__invert__");
        String *dunder_iter = get_or_create_interned_string_raw(L"__iter__");
        String *dunder_getitem =
            get_or_create_interned_string_raw(L"__getitem__");
        String *dunder_setitem =
            get_or_create_interned_string_raw(L"__setitem__");
        String *dunder_delitem =
            get_or_create_interned_string_raw(L"__delitem__");
        String *dunder_contains =
            get_or_create_interned_string_raw(L"__contains__");

        auto install_binary_arithmetic_operator_table =
            [this](OperatorDispatchTableId table_id, String *normal_dunder,
                   String *reflected_dunder) {
                std::array<OperatorStep, 6> &steps =
                    operator_dispatch_steps_[static_cast<size_t>(table_id)];
                steps = {{
                    OperatorStep::call_binary_reflected(
                        reflected_dunder,
                        OperatorStepApplicability::
                            IfArithmeticReflectedPriority,
                        2),
                    OperatorStep::call_binary(
                        normal_dunder,
                        OperatorStepApplicability::IfMethodFound),
                    OperatorStep::raise_unsupported(),
                    OperatorStep::call_binary(
                        normal_dunder,
                        OperatorStepApplicability::IfMethodFound),
                    OperatorStep::call_binary_reflected(
                        reflected_dunder,
                        OperatorStepApplicability::
                            IfMethodFoundAndOperands01TypesDiffer),
                    OperatorStep::raise_unsupported(),
                }};

                operator_dispatch_tables_[static_cast<size_t>(table_id)] =
                    OperatorDispatchTable{steps.data(),
                                          static_cast<uint8_t>(steps.size())};
            };

        auto install_ternary_arithmetic_operator_table =
            [this](OperatorDispatchTableId table_id, String *normal_dunder,
                   String *reflected_dunder) {
                std::array<OperatorStep, 6> &steps =
                    operator_dispatch_steps_[static_cast<size_t>(table_id)];
                steps = {{
                    OperatorStep::call_ternary_reflected(
                        reflected_dunder,
                        OperatorStepApplicability::
                            IfArithmeticReflectedPriority,
                        2),
                    OperatorStep::call_ternary(
                        normal_dunder,
                        OperatorStepApplicability::IfMethodFound),
                    OperatorStep::raise_unsupported(),
                    OperatorStep::call_ternary(
                        normal_dunder,
                        OperatorStepApplicability::IfMethodFound),
                    OperatorStep::call_ternary_reflected(
                        reflected_dunder,
                        OperatorStepApplicability::
                            IfMethodFoundAndOperands01TypesDiffer),
                    OperatorStep::raise_unsupported(),
                }};

                operator_dispatch_tables_[static_cast<size_t>(table_id)] =
                    OperatorDispatchTable{steps.data(),
                                          static_cast<uint8_t>(steps.size())};
            };

        register_truthiness_dispatch_handlers(*this);
        auto &truth_steps = operator_dispatch_steps_[static_cast<size_t>(
            OperatorDispatchTableId::Truthiness)];
        truth_steps[0] = OperatorStep::call_unary(
            get_or_create_interned_string_raw(L"__bool__"),
            OperatorStepApplicability::IfMethodFound, 0, TruthResultKind::Bool);
        truth_steps[1] = OperatorStep::call_unary(
            get_or_create_interned_string_raw(L"__len__"),
            OperatorStepApplicability::IfMethodFound, 0,
            TruthResultKind::Length);
        truth_steps[2] =
            OperatorStep{nullptr, OperatorStepAction::TruthDefaultTrue,
                         OperatorStepApplicability::Always};
        operator_dispatch_tables_[static_cast<size_t>(
            OperatorDispatchTableId::Truthiness)] =
            OperatorDispatchTable{truth_steps.data(), 3};

        auto install_unary_operator_table =
            [this](OperatorDispatchTableId table_id, String *dunder) {
                std::array<OperatorStep, 6> &steps =
                    operator_dispatch_steps_[static_cast<size_t>(table_id)];
                steps[0] = OperatorStep::call_unary(
                    dunder, OperatorStepApplicability::IfMethodFound);
                steps[1] = OperatorStep::raise_unsupported();

                operator_dispatch_tables_[static_cast<size_t>(table_id)] =
                    OperatorDispatchTable{steps.data(), 2};
            };

        auto install_receiver_binary_operator_table =
            [this](OperatorDispatchTableId table_id, String *dunder) {
                std::array<OperatorStep, 6> &steps =
                    operator_dispatch_steps_[static_cast<size_t>(table_id)];
                steps[0] = OperatorStep::call_binary(
                    dunder, OperatorStepApplicability::IfMethodFound);
                steps[1] = OperatorStep::raise_unsupported();

                operator_dispatch_tables_[static_cast<size_t>(table_id)] =
                    OperatorDispatchTable{steps.data(), 2};
            };

        auto install_receiver_ternary_operator_table =
            [this](OperatorDispatchTableId table_id, String *dunder) {
                std::array<OperatorStep, 6> &steps =
                    operator_dispatch_steps_[static_cast<size_t>(table_id)];
                steps[0] = OperatorStep::call_ternary(
                    dunder, OperatorStepApplicability::IfMethodFound);
                steps[1] = OperatorStep::raise_unsupported();

                operator_dispatch_tables_[static_cast<size_t>(table_id)] =
                    OperatorDispatchTable{steps.data(), 2};
            };

        auto install_membership_operator_table =
            [this](OperatorDispatchTableId table_id, String *dunder,
                   String *dunder_iter, String *dunder_getitem) {
                std::array<OperatorStep, 6> &steps =
                    operator_dispatch_steps_[static_cast<size_t>(table_id)];
                steps[0] = OperatorStep::call_binary(
                    dunder, OperatorStepApplicability::IfMethodFound);
                steps[1] =
                    OperatorStep::call_iter_membership_fallback(dunder_iter);
                steps[2] = OperatorStep::call_sequence_membership_fallback(
                    dunder_getitem);
                steps[3] = OperatorStep::raise_unsupported();

                operator_dispatch_tables_[static_cast<size_t>(table_id)] =
                    OperatorDispatchTable{steps.data(), 4};
            };

        install_binary_arithmetic_operator_table(OperatorDispatchTableId::Add,
                                                 dunder_add, dunder_radd);
        install_binary_arithmetic_operator_table(OperatorDispatchTableId::Sub,
                                                 dunder_sub, dunder_rsub);
        install_binary_arithmetic_operator_table(OperatorDispatchTableId::Mul,
                                                 dunder_mul, dunder_rmul);
        install_binary_arithmetic_operator_table(
            OperatorDispatchTableId::MatMul, dunder_matmul, dunder_rmatmul);
        install_binary_arithmetic_operator_table(
            OperatorDispatchTableId::BinaryPow, dunder_pow, dunder_rpow);
        install_ternary_arithmetic_operator_table(
            OperatorDispatchTableId::TernaryPow, dunder_pow, dunder_rpow);
        install_binary_arithmetic_operator_table(
            OperatorDispatchTableId::TrueDiv, dunder_truediv, dunder_rtruediv);
        install_binary_arithmetic_operator_table(
            OperatorDispatchTableId::FloorDiv, dunder_floordiv,
            dunder_rfloordiv);
        install_binary_arithmetic_operator_table(OperatorDispatchTableId::Mod,
                                                 dunder_mod, dunder_rmod);
        install_binary_arithmetic_operator_table(
            OperatorDispatchTableId::LShift, dunder_lshift, dunder_rlshift);
        install_binary_arithmetic_operator_table(
            OperatorDispatchTableId::RShift, dunder_rshift, dunder_rrshift);
        install_binary_arithmetic_operator_table(OperatorDispatchTableId::And,
                                                 dunder_and, dunder_rand);
        install_binary_arithmetic_operator_table(OperatorDispatchTableId::Xor,
                                                 dunder_xor, dunder_rxor);
        install_binary_arithmetic_operator_table(OperatorDispatchTableId::Or,
                                                 dunder_or, dunder_ror);
        install_unary_operator_table(OperatorDispatchTableId::Neg, dunder_neg);
        install_unary_operator_table(OperatorDispatchTableId::Pos, dunder_pos);
        install_unary_operator_table(OperatorDispatchTableId::Invert,
                                     dunder_invert);
        install_receiver_binary_operator_table(OperatorDispatchTableId::GetItem,
                                               dunder_getitem);
        install_receiver_ternary_operator_table(
            OperatorDispatchTableId::SetItem, dunder_setitem);
        install_receiver_binary_operator_table(OperatorDispatchTableId::DelItem,
                                               dunder_delitem);
        install_membership_operator_table(OperatorDispatchTableId::Contains,
                                          dunder_contains, dunder_iter,
                                          dunder_getitem);

        auto install_rich_compare_table = [this](
                                              OperatorDispatchTableId table_id,
                                              String *normal_dunder,
                                              String *reflected_dunder,
                                              OperatorStep fallback) {
            std::array<OperatorStep, 6> &steps =
                operator_dispatch_steps_[static_cast<size_t>(table_id)];
            steps = {{
                OperatorStep::call_binary_reflected(
                    reflected_dunder,
                    OperatorStepApplicability::
                        IfRichComparisonReflectedPriority,
                    2),
                OperatorStep::call_binary(
                    normal_dunder, OperatorStepApplicability::IfMethodFound),
                fallback,
                OperatorStep::call_binary(
                    normal_dunder, OperatorStepApplicability::IfMethodFound),
                OperatorStep::call_binary_reflected(
                    reflected_dunder, OperatorStepApplicability::IfMethodFound),
                fallback,
            }};

            operator_dispatch_tables_[static_cast<size_t>(table_id)] =
                OperatorDispatchTable{steps.data(),
                                      static_cast<uint8_t>(steps.size())};
        };

        install_rich_compare_table(OperatorDispatchTableId::CompareEq,
                                   dunder_eq, dunder_eq,
                                   OperatorStep::identity_eq());
        install_rich_compare_table(OperatorDispatchTableId::CompareNe,
                                   dunder_ne, dunder_ne,
                                   OperatorStep::identity_ne());
        install_rich_compare_table(OperatorDispatchTableId::CompareLt,
                                   dunder_lt, dunder_gt,
                                   OperatorStep::raise_ordering());
        install_rich_compare_table(OperatorDispatchTableId::CompareLe,
                                   dunder_le, dunder_ge,
                                   OperatorStep::raise_ordering());
        install_rich_compare_table(OperatorDispatchTableId::CompareGt,
                                   dunder_gt, dunder_lt,
                                   OperatorStep::raise_ordering());
        install_rich_compare_table(OperatorDispatchTableId::CompareGe,
                                   dunder_ge, dunder_le,
                                   OperatorStep::raise_ordering());
    }

    static void install_bootstrap_tuple_class_on_value(Value value,
                                                       ClassObject *tuple_class)
    {
        assert(has_native_layout<Tuple>(value));
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
            bool inserted = builtin_classes_by_name_
                                .emplace(definition.cls->get_name().extract(),
                                         definition.cls)
                                .second;
            assert(inserted);
            (void)inserted;
            builtin_classes.push_back(definition);
        };

        dunder_class_name_ = get_or_create_interned_string_raw(L"__class__");
        BuiltinClassDefinition type_definition = make_type_class(this);
        type_class_ = type_definition.cls;
        register_builtin_class(type_definition);

        register_builtin_class(make_object_class(this));
        register_builtin_class(make_str_class(this));
        install_bootstrap_string_class();
        initialize_operator_dispatch_tables();
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
            0, 0, immutable_shape_flags());
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
            0, 0, immutable_shape_flags());
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
            0, 0, immutable_shape_flags());
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
                0, 0, immutable_shape_flags());
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
            0, 0, immutable_shape_flags());
        register_builtin_class(make_list_class(this));
        register_builtin_class(make_bytes_class(this));
        register_builtin_class(make_dict_class(this));
        register_builtin_class(make_slotdict_class(this));
        register_builtin_class(make_slice_class(this));
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
        register_builtin_class(make_enumerate_class(this));
        register_builtin_class(make_zip_class(this));
        register_builtin_class(make_string_iterator_class(this));
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
        register_builtin_class(make_exception_subclass(
            this, L"KeyboardInterrupt", base_exception));
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
        BuiltinClassDefinition name_error_definition =
            make_exception_subclass(this, L"NameError", exception);
        ClassObject *name_error = name_error_definition.cls;
        register_builtin_class(name_error_definition);
        register_builtin_class(
            make_exception_subclass(this, L"UnboundLocalError", name_error));
        BuiltinClassDefinition type_error_definition =
            make_exception_subclass(this, L"TypeError", exception);
        ClassObject *type_error = type_error_definition.cls;
        register_builtin_class(type_error_definition);
        register_builtin_class(
            make_exception_subclass(this, L"ValueError", exception));
        BuiltinClassDefinition arithmetic_error_definition =
            make_exception_subclass(this, L"ArithmeticError", exception);
        ClassObject *arithmetic_error = arithmetic_error_definition.cls;
        register_builtin_class(arithmetic_error_definition);
        register_builtin_class(make_exception_subclass(
            this, L"FloatingPointError", arithmetic_error));
        register_builtin_class(
            make_exception_subclass(this, L"OverflowError", arithmetic_error));
        register_builtin_class(make_exception_subclass(
            this, L"ZeroDivisionError", arithmetic_error));
        BuiltinClassDefinition syntax_error_definition =
            make_exception_subclass(this, L"SyntaxError", exception);
        ClassObject *syntax_error = syntax_error_definition.cls;
        register_builtin_class(syntax_error_definition);
        BuiltinClassDefinition indentation_error_definition =
            make_exception_subclass(this, L"IndentationError", syntax_error);
        ClassObject *indentation_error = indentation_error_definition.cls;
        register_builtin_class(indentation_error_definition);
        register_builtin_class(
            make_exception_subclass(this, L"TabError", indentation_error));
        register_builtin_class(
            make_exception_subclass(this, L"SystemError", exception));
        BuiltinClassDefinition runtime_error_definition =
            make_exception_subclass(this, L"RuntimeError", exception);
        ClassObject *runtime_error = runtime_error_definition.cls;
        register_builtin_class(runtime_error_definition);
        register_builtin_class(
            make_exception_subclass(this, L"RecursionError", runtime_error));
        register_builtin_class(make_exception_subclass(
            this, L"PythonFinalizationError", runtime_error));
        register_builtin_class(make_exception_subclass(
            this, L"NotImplementedError", runtime_error));
        register_builtin_class(
            make_exception_subclass(this, L"AssertionError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"AttributeError", exception));
        BuiltinClassDefinition lookup_error_definition =
            make_exception_subclass(this, L"LookupError", exception);
        ClassObject *lookup_error = lookup_error_definition.cls;
        register_builtin_class(lookup_error_definition);
        register_builtin_class(
            make_exception_subclass(this, L"IndexError", lookup_error));
        register_builtin_class(
            make_exception_subclass(this, L"KeyError", lookup_error));
        register_builtin_class(make_stop_iteration_class(this, exception));
        auto os_error_definition =
            make_os_error_class(this, L"OSError", exception, true);
        ClassObject *os_error = os_error_definition.cls;
        register_builtin_class(os_error_definition);
        for(const wchar_t *name:
            {L"BlockingIOError", L"ChildProcessError", L"FileExistsError",
             L"FileNotFoundError", L"InterruptedError", L"IsADirectoryError",
             L"NotADirectoryError", L"PermissionError", L"ProcessLookupError",
             L"TimeoutError"})
            register_builtin_class(make_os_error_class(this, name, os_error));
        auto connection_definition =
            make_os_error_class(this, L"ConnectionError", os_error);
        ClassObject *connection = connection_definition.cls;
        register_builtin_class(connection_definition);
        for(const wchar_t *name:
            {L"BrokenPipeError", L"ConnectionAbortedError",
             L"ConnectionRefusedError", L"ConnectionResetError"})
            register_builtin_class(make_os_error_class(this, name, connection));
        register_builtin_class(
            make_exception_subclass(this, L"BufferError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"EOFError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"MemoryError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"ReferenceError", exception));
        BuiltinClassDefinition warning_definition =
            make_exception_subclass(this, L"Warning", exception);
        ClassObject *warning = warning_definition.cls;
        register_builtin_class(warning_definition);
        register_builtin_class(
            make_exception_subclass(this, L"BytesWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"DeprecationWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"EncodingWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"FutureWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"ImportWarning", warning));
        register_builtin_class(make_exception_subclass(
            this, L"PendingDeprecationWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"ResourceWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"RuntimeWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"SyntaxWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"UnicodeWarning", warning));
        register_builtin_class(
            make_exception_subclass(this, L"UserWarning", warning));

        install_object_class_methods(this);
        install_exception_class_methods(this);
        install_os_error_methods(this);
        install_str_class_methods(this);
        install_bytes_class_methods(this);
        install_int_class_methods(this);
        install_bool_class_methods(this);
        install_none_type_class_methods(this);
        install_not_implemented_type_class_methods(this);
        install_ellipsis_type_class_methods(this);
        install_list_class_methods(this);
        install_tuple_class_methods(this);
        install_dict_class_methods(this, type_error);
        dict_insert_if_absent_known_hash_function_ =
            Optional<TValue<Function>>::some(unwrap_bootstrap_expected(
                this, make_dict_insert_if_absent_known_hash_function(this),
                "creating keyword helper"));
        call_keyword_error_function_ =
            Optional<TValue<Function>>::some(unwrap_bootstrap_expected(
                this, make_call_keyword_error_function(this),
                "creating keyword helper"));
        call_keyword_merge_function_ =
            Optional<TValue<Function>>::some(unwrap_bootstrap_expected(
                this, make_call_keyword_merge_function(this),
                "creating keyword helper"));
        call_keyword_add_function_ =
            Optional<TValue<Function>>::some(unwrap_bootstrap_expected(
                this, make_call_keyword_add_function(this),
                "creating keyword helper"));
        install_slotdict_class_methods(this);
        install_slice_class_methods(this);
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
        bool installed_builtins_loader = unwrap_bootstrap_expected(
            active_vm(),
            builtins_module.extract()->set_own_property(
                get_or_create_interned_string_value(L"__loader__"),
                Value::from_oop(builtins_loader)),
            "installing module binding");
        bool installed_builtins_spec = unwrap_bootstrap_expected(
            active_vm(),
            builtins_module.extract()->set_own_property(
                get_or_create_interned_string_value(L"__spec__"),
                Value::from_oop(builtins_spec)),
            "installing module binding");
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
        bool installed_modules = unwrap_bootstrap_expected(
            active_vm(),
            sys_module_->set_own_property(modules_name,
                                          Value::from_oop(imported_modules_)),
            "installing module binding");
        bool installed_path = unwrap_bootstrap_expected(
            active_vm(),
            sys_module_->set_own_property(path_name, Value::from_oop(path)),
            "installing module binding");
        assert(installed_modules);
        assert(installed_path);
        (void)installed_modules;
        (void)installed_path;

        install_sys_static_attributes(this, sys_module_);

        unwrap_bootstrap_expected(
            this,
            imported_modules_->set_item_for_str(get_default_thread(), sys_name,
                                                Value::from_oop(sys_module_)),
            "installing sys module");
        unwrap_bootstrap_expected(this,
                                  imported_modules_->set_item_for_str(
                                      get_default_thread(), builtins_name,
                                      builtins_module.raw_value()),
                                  "installing builtins module");
    }

    void VirtualMachine::initialize_builtins()
    {
        std::vector<BuiltinClassDefinition> builtin_classes =
            initialize_builtin_types();
        get_default_thread()->refresh_class_for_native_layout_cache();

        initialize_module_bootstrap();

        ModuleObject *builtins_module = global_builtins_module().extract();
        // This module is VM-owned but deliberately absent from import
        // discovery.
        builtin_implementation_module_ = make_immortal_object_raw<ModuleObject>(
            get_or_create_interned_string_value(L"_clover_builtin_impl"),
            make_object_value<Dict>().raw_value(), Value::None(),
            get_or_create_interned_string_value(L"").raw_value(), Value::None(),
            Value::None(), Value::not_present());
        ModuleObject *implementation_module = builtin_implementation_module_;
        auto install_implementation_binding = [&](TValue<String> name,
                                                  Value value) {
            bool installed = unwrap_bootstrap_expected(
                active_vm(),
                implementation_module->set_own_property(name, value),
                "installing module binding");
            assert(installed);
            (void)installed;
        };

        auto install_builtin_binding = [&](TValue<String> name, Value value) {
            bool installed = unwrap_bootstrap_expected(
                active_vm(), builtins_module->set_own_property(name, value),
                "installing module binding");
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
        ThreadState *thread = get_default_thread();
        range_builtin = unwrap_bootstrap_expected(
                            this,
                            make_intrinsic_function(
                                this, builtin_range,
                                Optional<TValue<Tuple>>::some(range_defaults)),
                            "installing range")
                            .raw_value();
        install_builtin_binding(range_name, range_builtin);
        for(const wchar_t *alias: {L"IOError", L"EnvironmentError"})
            install_builtin_binding(get_or_create_interned_string_value(alias),
                                    Value::from_oop(class_for_native_layout(
                                        NativeLayoutId::OSError)));
        unwrap_bootstrap_expected(
            this,
            install_builtin_function_bindings(this, implementation_module),
            "installing builtin functions");

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
            unwrap_bootstrap_expected(
                this,
                make_intrinsic_function(
                    this, builtin_import,
                    Optional<TValue<Tuple>>::some(import_defaults)),
                "installing __import__")
                .raw_value());

        hash_value_helper_function_ =
            Optional<TValue<Function>>::some(unwrap_bootstrap_expected(
                this, make_hash_value_helper_function(this),
                "creating hash value helper"));
        test_equal_helper_function_ =
            Optional<TValue<Function>>::some(unwrap_bootstrap_expected(
                this, make_test_equal_helper_function(this),
                "creating test equal helper"));

        const OrderingOp ordering_operations[] = {
            OrderingOp::Less, OrderingOp::LessEqual, OrderingOp::Greater,
            OrderingOp::GreaterEqual};
        for(size_t i = 0; i < std::size(ordering_operations); ++i)
        {
            ordering_helper_functions_[i] =
                Optional<TValue<Function>>::some(unwrap_bootstrap_expected(
                    this,
                    make_ordering_helper_function(this, ordering_operations[i]),
                    "creating ordering helper"));
        }

        // Retain the native allocator without creating a class lookup cache:
        // installing Python dict methods later replaces the __new__ slot.
        install_implementation_binding(
            get_or_create_interned_string_value(L"_clover_dict_allocate"),
            dict_class()->get_own_property(interned_string(L"__new__")));

        install_implementation_binding(
            get_or_create_interned_string_value(
                L"_clover_raise_duplicate_keyword"),
            call_keyword_error_function().raw_value());
        install_implementation_binding(
            get_or_create_interned_string_value(
                L"_clover_raise_call_mapping_error"),
            unwrap_bootstrap_expected(
                this,
                make_intrinsic_function(this, raise_call_keyword_mapping_error),
                "creating mapping error helper")
                .raw_value());

        install_enumerate_helpers(this, implementation_module);
        install_zip_helpers(this, implementation_module);

        // Seed dependencies explicitly: empty __builtins__ disables public
        // fallback.
        const wchar_t *seed_names[] = {
            L"list",           L"tuple",      L"slice",
            L"dict",           L"int",        L"str",
            L"object",         L"isinstance", L"TypeError",
            L"ValueError",     L"IndexError", L"StopIteration",
            L"NotImplemented", L"True",       L"False",
            L"None",           L"Ellipsis",   L"AttributeError"};
        for(const wchar_t *name: seed_names)
        {
            TValue<String> key = get_or_create_interned_string_value(name);
            Value value = builtins_module->get_own_property(key);
            assert(!value.is_not_present());
            install_implementation_binding(key, value);
        }
        install_implementation_binding(
            get_or_create_interned_string_value(L"sys"),
            sys_module().raw_value());

        Expected<CodeObject *> builtins_code = thread->compile_in_module(
            trusted_builtin_source, StartRule::File, implementation_module,
            LanguageMode::TrustedCloverExtensions);
        if(builtins_code.has_exception())
        {
            fatal_bootstrap_python_exception(
                thread, "compiling trusted builtin_implementations.py");
        }
        Value result = thread->run_clovervm_code_object(builtins_code.value());
        if(result.is_exception_marker())
        {
            fatal_bootstrap_python_exception(
                thread, "initializing trusted builtin_implementations.py");
        }

        Value enumerate_call = implementation_module->get_own_property(
            get_or_create_interned_string_value(L"_clover_enumerate_call"));
        if(!has_native_layout<Function>(enumerate_call))
        {
            fatal(
                "trusted builtin source did not define enumerate constructor");
        }
        enumerate_call_function_ = Optional<TValue<Function>>::some(
            TValue<Function>::from_value_assumed(enumerate_call));

        Value to_exact_tuple = implementation_module->get_own_property(
            get_or_create_interned_string_value(
                L"_clover_iterable_to_exact_tuple"));
        if(!has_native_layout<Function>(to_exact_tuple))
        {
            fatal("trusted builtin_implementations.py did not define "
                  "exact-tuple helper");
        }
        iterable_to_exact_tuple_function_ = Optional<TValue<Function>>::some(
            TValue<Function>::from_value_assumed(to_exact_tuple));

        Value to_exact_list = implementation_module->get_own_property(
            get_or_create_interned_string_value(
                L"_clover_iterable_to_exact_list"));
        if(!has_native_layout<Function>(to_exact_list))
        {
            fatal("trusted builtin_implementations.py did not define "
                  "exact-list helper");
        }
        iterable_to_exact_list_function_ = Optional<TValue<Function>>::some(
            TValue<Function>::from_value_assumed(to_exact_list));

        Value keyword_mapping = implementation_module->get_own_property(
            get_or_create_interned_string_value(
                L"_clover_merge_call_keyword_mapping"));
        if(!has_native_layout<Function>(keyword_mapping))
        {
            fatal("trusted builtin_implementations.py did not define keyword "
                  "mapping helper");
        }
        call_keyword_mapping_function_ = Optional<TValue<Function>>::some(
            TValue<Function>::from_value_assumed(keyword_mapping));

        Value list_extend = implementation_module->get_own_property(
            get_or_create_interned_string_value(L"_clover_list_extend"));
        if(!has_native_layout<Function>(list_extend))
        {
            fatal("trusted builtin_implementations.py did not define "
                  "list-extend helper");
        }
        list_extend_function_ = Optional<TValue<Function>>::some(
            TValue<Function>::from_value_assumed(list_extend));

        const wchar_t *export_names[] = {
            L"abs",     L"iter",    L"next",    L"repr", L"hash",
            L"len",     L"getattr", L"hasattr", L"vars", L"dir",
            L"globals", L"locals",  L"print",   L"pow",  L"sum",
            L"any",     L"all",     L"min",     L"max",  L"sorted"};
        for(const wchar_t *name: export_names)
        {
            TValue<String> key = get_or_create_interned_string_value(name);
            Value function = implementation_module->get_own_property(key);
            if(!has_native_layout<Function>(function))
            {
                fatal("trusted builtin_implementations.py did not define "
                      "public builtin");
            }
            install_builtin_binding(key, function);
        }
        install_dict_python_methods(this, implementation_module);
        install_list_python_methods(this, implementation_module);
        install_tuple_python_methods(this, implementation_module);
        install_enumerate_python_methods(this, implementation_module);
        install_zip_python_methods(this, implementation_module);
        install_slice_python_methods(this, implementation_module);

        TValue<String> membership_iter_fallback_name =
            get_or_create_interned_string_value(
                L"_clover_iter_membership_fallback");
        Value membership_iter_fallback_value =
            implementation_module->get_own_property(
                membership_iter_fallback_name);
        if(!has_native_layout<Function>(membership_iter_fallback_value))
        {
            fatal("trusted builtin_implementations.py did not define iter "
                  "membership "
                  "fallback");
        }
        membership_iter_fallback_function_ = Optional<TValue<Function>>::some(
            TValue<Function>::from_value_assumed(
                membership_iter_fallback_value));

        TValue<String> membership_sequence_fallback_name =
            get_or_create_interned_string_value(
                L"_clover_sequence_membership_fallback");
        Value membership_sequence_fallback_value =
            implementation_module->get_own_property(
                membership_sequence_fallback_name);
        if(!has_native_layout<Function>(membership_sequence_fallback_value))
        {
            fatal("trusted builtin_implementations.py did not define sequence "
                  "membership "
                  "fallback");
        }
        membership_sequence_fallback_function_ =
            Optional<TValue<Function>>::some(
                TValue<Function>::from_value_assumed(
                    membership_sequence_fallback_value));

        Expected<CodeObject *> sys_code = thread->compile_in_module(
            trusted_sys_source, StartRule::File, sys_module().extract(),
            LanguageMode::TrustedCloverExtensions);
        if(sys_code.has_exception())
        {
            fatal_bootstrap_python_exception(thread,
                                             "compiling trusted sys.py");
        }
        result = thread->run_clovervm_code_object(sys_code.value());
        if(result.is_exception_marker())
        {
            fatal_bootstrap_python_exception(thread,
                                             "initializing trusted sys.py");
        }

        for(const BuiltinClassDefinition &definition: builtin_classes)
        {
            definition.cls->freeze();
        }
    }

    TValue<Function> VirtualMachine::enumerate_call_function() const
    {
        assert(enumerate_call_function_.value().has_value());
        return enumerate_call_function_.value().value();
    }

    TValue<Function> VirtualMachine::iterable_to_exact_tuple_function() const
    {
        assert(iterable_to_exact_tuple_function_.value().has_value());
        return iterable_to_exact_tuple_function_.value().value();
    }

    TValue<Function> VirtualMachine::iterable_to_exact_list_function() const
    {
        assert(iterable_to_exact_list_function_.value().has_value());
        return iterable_to_exact_list_function_.value().value();
    }

    TValue<Function>
    VirtualMachine::dict_insert_if_absent_known_hash_function() const
    {
        assert(dict_insert_if_absent_known_hash_function_.value().has_value());
        return dict_insert_if_absent_known_hash_function_.value().value();
    }

    TValue<Function> VirtualMachine::call_keyword_error_function() const
    {
        assert(call_keyword_error_function_.value().has_value());
        return call_keyword_error_function_.value().value();
    }

    TValue<Function> VirtualMachine::call_keyword_mapping_function() const
    {
        assert(call_keyword_mapping_function_.value().has_value());
        return call_keyword_mapping_function_.value().value();
    }

    TValue<Function> VirtualMachine::call_keyword_merge_function() const
    {
        assert(call_keyword_merge_function_.value().has_value());
        return call_keyword_merge_function_.value().value();
    }

    TValue<Function> VirtualMachine::call_keyword_add_function() const
    {
        assert(call_keyword_add_function_.value().has_value());
        return call_keyword_add_function_.value().value();
    }

    TValue<Function> VirtualMachine::list_extend_function() const
    {
        assert(list_extend_function_.value().has_value());
        return list_extend_function_.value().value();
    }

    TValue<Function> VirtualMachine::membership_iter_fallback_function() const
    {
        assert(membership_iter_fallback_function_.value().has_value());
        return membership_iter_fallback_function_.value().value();
    }

    Expected<TValue<Dict>> VirtualMachine::os_error_errno_map()
    {
        if(!os_error_errno_map_.value().has_value())
        {
            TValue<Dict> mapping =
                CL_TRY(make_os_error_errno_map(active_thread()));
            os_error_errno_map_ = Optional<TValue<Dict>>::some(mapping);
        }
        return Expected<TValue<Dict>>::ok(os_error_errno_map_.value().value());
    }

    TValue<Function> VirtualMachine::hash_value_helper_function() const
    {
        assert(hash_value_helper_function_.value().has_value());
        return hash_value_helper_function_.value().value();
    }

    TValue<Function> VirtualMachine::test_equal_helper_function() const
    {
        assert(test_equal_helper_function_.value().has_value());
        return test_equal_helper_function_.value().value();
    }

    TValue<Function>
    VirtualMachine::ordering_helper_function(OrderingOp operation) const
    {
        size_t index;
        switch(operation)
        {
            case OrderingOp::Less:
                index = 0;
                break;
            case OrderingOp::LessEqual:
                index = 1;
                break;
            case OrderingOp::Greater:
                index = 2;
                break;
            case OrderingOp::GreaterEqual:
                index = 3;
                break;
            default:
                fatal("invalid ordering helper operation");
        }
        assert(ordering_helper_functions_[index].value().has_value());
        return ordering_helper_functions_[index].value().value();
    }

    TValue<Function>
    VirtualMachine::membership_sequence_fallback_function() const
    {
        assert(membership_sequence_fallback_function_.value().has_value());
        return membership_sequence_fallback_function_.value().value();
    }

}  // namespace cl
