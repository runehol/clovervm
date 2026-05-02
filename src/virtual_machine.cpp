#include "virtual_machine.h"
#include "code_object.h"
#include "dict.h"
#include "function.h"
#include "instance.h"
#include "list.h"
#include "native_function.h"
#include "range_iterator.h"
#include "scope.h"
#include "thread_state.h"
#include "tuple.h"
#include <cassert>
#include <initializer_list>
#include <stdexcept>

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

    static TValue<CLInt> require_range_integer_arg(Value arg)
    {
        if(!arg.is_integer())
        {
            throw std::runtime_error(
                "TypeError: range() arguments must be integers");
        }
        return TValue<CLInt>::from_value_unchecked(arg);
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

        start = require_range_integer_arg(start);
        stop = require_range_integer_arg(stop);
        step = require_range_integer_arg(step);
        if(step.get_smi() == 0)
        {
            throw std::runtime_error(
                "ValueError: range() arg 3 must not be zero");
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
        initialize_builtin_scope();
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

    ClassObject *class_for_native_layout(VirtualMachine *vm, NativeLayoutId id)
    {
        return vm->class_for_native_layout(id);
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
        register_builtin_class(make_list_class(this));
        register_builtin_class(make_dict_class(this));
        register_builtin_class(make_function_class(this));
        register_builtin_class(make_code_object_class(this));
        register_builtin_class(make_range_iterator_class(this));
        install_str_class_methods(this);

        for(ClassObject *cls: builtin_classes)
        {
            assert(cls != nullptr);
            cls->write_storage_location(
                StorageLocation{ClassObject::kClassMetadataSlotClass,
                                StorageKind::Inline},
                Value::from_oop(type));
        }
    }

    void VirtualMachine::initialize_builtin_scope()
    {
        initialize_builtin_types();

        builtin_scope = HeapPtr<Scope>(
            refcounted_global_heap.make_global_internal_raw<Scope>(nullptr));

        for(ClassObject *cls: builtin_classes)
        {
            if(cls == code_class())
            {
                continue;
            }
            builtin_scope.extract()->set_by_name(cls->get_name(),
                                                 Value::from_oop(cls));
        }

        TValue<String> range_name =
            get_or_create_interned_string_value(L"range");
        TValue<Tuple> range_defaults = make_object_value<Tuple>(2);
        range_defaults.extract()->initialize_item_unchecked(0, Value::None());
        range_defaults.extract()->initialize_item_unchecked(1, Value::None());
        range_builtin =
            make_native_function(this, builtin_range, range_defaults);
        builtin_scope.extract()->set_by_name(range_name, range_builtin);
    }

}  // namespace cl
