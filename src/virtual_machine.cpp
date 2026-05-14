#include "virtual_machine.h"
#include "bool.h"
#include "clover_entry.h"
#include "code_object.h"
#include "codegen.h"
#include "dict.h"
#include "exception_object.h"
#include "exception_propagation.h"
#include "function.h"
#include "instance.h"
#include "int.h"
#include "list.h"
#include "list_iterator.h"
#include "native_function.h"
#include "none_type.h"
#include "parser.h"
#include "range_iterator.h"
#include "scope.h"
#include "shape.h"
#include "thread_state.h"
#include "tuple.h"
#include "tuple_iterator.h"
#include <cassert>
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
            TValue<CLInt>::from_value_unchecked(start),
            TValue<CLInt>::from_value_unchecked(stop),
            TValue<CLInt>::from_value_unchecked(step));
    }

    VirtualMachine::VirtualMachine()
        : refcounted_global_heap(GlobalHeap::refcounted_heap()),
          interned_global_heap(GlobalHeap::interned_heap()),
          interned_strings(&interned_global_heap)
    {
        // make the main thread
        ThreadState *default_thread = make_new_thread();
        ThreadState::ActivationScope activation_scope(default_thread);
        try
        {
            initialize_builtin_scope();
        }
        catch(...)
        {
            range_builtin.clear();
            builtin_scope.clear();
            throw;
        }
    }

    VirtualMachine::~VirtualMachine()
    {
        if(!threads.empty())
        {
            ThreadState::ActivationScope activation_scope(threads[0].get());
            range_builtin.clear();
            builtin_scope.clear();
        }
    }

    ThreadState *VirtualMachine::make_new_thread()
    {
        return threads.emplace_back(std::make_unique<ThreadState>(this)).get();
    }

    void VirtualMachine::complete_safepoint()
    {
        run_safepoint_reclamation();
        clear_safepoint_request();
        if(fire_every_safepoint_for_testing())
        {
            request_safepoint();
        }
    }

    void VirtualMachine::run_safepoint_reclamation() {}

    ClassObject *class_for_native_layout(VirtualMachine *vm, NativeLayoutId id)
    {
        return vm->class_for_native_layout(id);
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
                cls->read_storage_location(StorageLocation{
                    ClassObject::kClassMetadataSlotBases, StorageKind::Inline}),
                tuple);
            install_bootstrap_tuple_class_on_value(
                cls->read_storage_location(StorageLocation{
                    ClassObject::kClassMetadataSlotMro, StorageKind::Inline}),
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
            descriptor_flag(DescriptorFlag::ShapeClassValue);
        smi_shape_ = Shape::make_immortal_root_with_single_descriptor(
            this, Value::from_oop(int_class_), dunder_class_name(),
            DescriptorInfo::make(StorageLocation::not_found(),
                                 inline_class_flags),
            0, fixed_attribute_shape_flags());
        BuiltinClassDefinition bool_definition =
            make_bool_class(this, int_class_);
        bool_class_ = bool_definition.cls;
        register_builtin_class(bool_definition);
        bool_shape_ = Shape::make_immortal_root_with_single_descriptor(
            this, Value::from_oop(bool_class_), dunder_class_name(),
            DescriptorInfo::make(StorageLocation::not_found(),
                                 inline_class_flags),
            0, fixed_attribute_shape_flags());
        BuiltinClassDefinition none_type_definition =
            make_none_type_class(this);
        none_type_class_ = none_type_definition.cls;
        register_builtin_class(none_type_definition);
        none_shape_ = Shape::make_immortal_root_with_single_descriptor(
            this, Value::from_oop(none_type_class_), dunder_class_name(),
            DescriptorInfo::make(StorageLocation::not_found(),
                                 inline_class_flags),
            0, fixed_attribute_shape_flags());
        register_builtin_class(make_list_class(this));
        register_builtin_class(make_dict_class(this));
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
        register_builtin_class(
            make_exception_subclass(this, L"NameError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"TypeError", exception));
        register_builtin_class(
            make_exception_subclass(this, L"ValueError", exception));
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
    }

    void VirtualMachine::initialize_builtin_scope()
    {
        initialize_builtin_types();

        builtin_scope = HeapPtr<Scope>(
            get_default_thread()->make_internal_raw<Scope>(nullptr));

        for(ClassObject *cls: builtin_classes)
        {
            if(cls == code_class())
            {
                continue;
            }
            builtin_scope.extract()->set_by_name(cls->get_name(),
                                                 Value::from_oop(cls));
        }

        builtin_scope.extract()->set_by_name(
            get_or_create_interned_string_value(L"True"), Value::True());
        builtin_scope.extract()->set_by_name(
            get_or_create_interned_string_value(L"False"), Value::False());
        builtin_scope.extract()->set_by_name(
            get_or_create_interned_string_value(L"None"), Value::None());

        TValue<String> range_name =
            get_or_create_interned_string_value(L"range");
        TValue<Tuple> range_defaults = make_object_value<Tuple>(2);
        range_defaults.extract()->initialize_item_unchecked(0, Value::None());
        range_defaults.extract()->initialize_item_unchecked(1, Value::None());
        range_builtin =
            make_native_function(this, builtin_range, range_defaults);
        builtin_scope.extract()->set_by_name(range_name, range_builtin);

        ThreadState *thread = get_default_thread();
        CodeObject *builtins_code = thread->compile_in_scope(
            trusted_builtin_source, StartRule::File, L"<builtins>",
            builtin_scope.extract(), LanguageMode::TrustedCloverExtensions);
        Value result = thread->run_clovervm_code_object(builtins_code);
        if(result.is_exception_marker())
        {
            throw std::runtime_error(
                "failed to initialize trusted builtins.py");
        }
    }

}  // namespace cl
