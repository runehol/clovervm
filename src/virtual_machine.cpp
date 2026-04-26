#include "virtual_machine.h"
#include "builtin_function.h"
#include "code_object.h"
#include "dict.h"
#include "function.h"
#include "instance.h"
#include "list.h"
#include "range_iterator.h"
#include "scope.h"
#include "thread_state.h"
#include "tuple.h"
#include <cassert>
#include <stdexcept>

namespace cl
{
    static TValue<CLInt> require_range_integer_arg(const CallArguments &args,
                                                   uint32_t index)
    {
        if(!args[index].is_integer())
        {
            throw std::runtime_error(
                "TypeError: range() arguments must be integers");
        }
        return TValue<CLInt>(args[index]);
    }

    static Value builtin_range(ThreadState *thread, const CallArguments &args)
    {
        Value start = Value::from_smi(0);
        Value stop = Value::None();
        Value step = Value::from_smi(1);

        switch(args.n_args)
        {
            case 1:
                stop = require_range_integer_arg(args, 0);
                break;

            case 2:
                start = require_range_integer_arg(args, 0);
                stop = require_range_integer_arg(args, 1);
                break;

            case 3:
                start = require_range_integer_arg(args, 0);
                stop = require_range_integer_arg(args, 1);
                step = require_range_integer_arg(args, 2);
                if(step.get_smi() == 0)
                {
                    throw std::runtime_error(
                        "ValueError: range() arg 3 must not be zero");
                }
                break;

            default:
                throw std::runtime_error(
                    "TypeError: wrong number of arguments");
        }

        return thread->make_object_value<RangeIterator>(
            TValue<CLInt>(start), TValue<CLInt>(stop), TValue<CLInt>(step));
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
        register_builtin_class(make_builtin_function_class(this));
        register_builtin_class(make_str_class(this));
        install_bootstrap_string_class();
        register_builtin_class(make_tuple_class(this));
        install_bootstrap_tuple_class();
        register_builtin_class(make_list_class(this));
        register_builtin_class(make_dict_class(this));
        register_builtin_class(make_function_class(this));
        register_builtin_class(make_code_object_class(this));
        register_builtin_class(make_range_iterator_class(this));
        register_builtin_class(make_object_class(this));

        ClassObject *type = type_class();
        assert(type != nullptr);
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
        range_builtin =
            refcounted_global_heap.make_global_internal_value<BuiltinFunction>(
                builtin_function_class(), builtin_range, 1, 3);
        builtin_scope.extract()->set_by_name(range_name, range_builtin);
    }

}  // namespace cl
