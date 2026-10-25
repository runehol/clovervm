#include "builtin_types/enumerate.h"
#include "builtin_types/int.h"
#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

namespace cl
{
    [[nodiscard]] static Value validate_class(ThreadState *thread, Value value)
    {
        ClassObject *cls = try_native_cast<ClassObject>(value);
        if(cls == nullptr ||
           !is_subclass_of(cls, thread->get_machine()->enumerate_class()) ||
           cls->instance_native_layout_id() !=
               NativeLayoutId::EnumerateIterator)
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError",
                L"enumerate.__new__ expects an enumerate subclass");
        }
        return value;
    }

    [[nodiscard]] static Value iterator_iter(ThreadState *thread, Value self)
    {
        (void)thread;
        (void)CL_TRY(TValue<EnumerateIterator>::from_value_or_raise(
            self, L"TypeError",
            L"enumerate.__iter__ expects an enumerate receiver"));
        return self;
    }

    [[nodiscard]] static Value normalize_start(ThreadState *thread, Value value)
    {
        if(!is_intlike_value(value))
        {
            std::wstring message = L"'";
            message.append(
                string_view(thread->class_of_value(value)->get_name()));
            message += L"' object cannot be interpreted as an integer";
            return thread->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        // Normalize bool and integer subclasses without invoking overrides.
        if(value.is_smi())
            return value;
        if(has_native_layout<BigInt>(value) &&
           native_cast<BigInt>(value)->get_shape()->get_class() ==
               thread->get_machine()->int_class())
            return value;
        SmiViewStorage storage;
        return CL_TRY(finalize_bigint(
            thread, intlike_value_bigint_view(value, &storage)));
    }

    [[nodiscard]] static Value allocate(ThreadState *thread, Value cls,
                                        Value source, Value index)
    {
        CL_PROPAGATE_EXCEPTION(validate_class(thread, cls));
        return thread
            ->make_internal_value<EnumerateIterator>(
                native_cast<ClassObject>(cls), source, index)
            .raw_value();
    }

    [[nodiscard]] static Value source(ThreadState *thread, Value self)
    {
        (void)thread;
        auto iterator = CL_TRY(TValue<EnumerateIterator>::from_value_or_raise(
            self, L"TypeError",
            L"enumerate.__next__ expects an enumerate receiver"));
        return iterator.extract()->source.raw_value();
    }

    [[nodiscard]] static Value take_index(ThreadState *thread, Value self)
    {
        auto iterator = CL_TRY(TValue<EnumerateIterator>::from_value_or_raise(
            self, L"TypeError",
            L"enumerate.__next__ expects an enumerate receiver"));
        // The source callback has returned. Re-read the shared counter now so
        // a nested next(self) gets the earlier index. Retain the old index
        // across replacement of the Member edge and any bigint allocation.
        Owned<Value> index(iterator.extract()->next_index.value());
        Value incremented;
        if(index.raw_value().is_smi() &&
           index.raw_value().get_smi() < value_smi_max)
            incremented = Value::from_smi(index.raw_value().get_smi() + 1);
        else
        {
            SmiViewStorage left_storage;
            SmiViewStorage right_storage;
            incremented = CL_TRY(bigint_add(
                thread,
                intlike_value_bigint_view(index.raw_value(), &left_storage),
                smi_bigint_view(1, &right_storage)));
        }
        iterator.extract()->next_index = incremented;
        return index.raw_value();
    }

    BuiltinClassDefinition make_enumerate_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId layouts[] = {
            NativeLayoutId::EnumerateIterator};
        ClassObject *cls = ClassObject::make_builtin_class<EnumerateIterator>(
            vm->get_or_create_interned_string_value(L"enumerate"), 0, nullptr,
            0, vm->object_class(), SubclassPolicy::Allowed,
            immutable_shape_flags());
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__iter__", iterator_iter,
                                     L"Return self."),
        };
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, cls, methods, std::size(methods)),
                                  "installing enumerate methods");
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Public);
    }

    void install_enumerate_helpers(VirtualMachine *vm, ModuleObject *module)
    {
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_enumerate_validate_class"),
                unwrap_bootstrap_expected(
                    vm, make_intrinsic_function(vm, validate_class),
                    "creating enumerate helper")
                    .raw_value()),
            "installing enumerate helper");
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_enumerate_normalize_start"),
                unwrap_bootstrap_expected(
                    vm, make_intrinsic_function(vm, normalize_start),
                    "creating enumerate helper")
                    .raw_value()),
            "installing enumerate helper");
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_enumerate_allocate"),
                unwrap_bootstrap_expected(vm,
                                          make_intrinsic_function(vm, allocate),
                                          "creating enumerate helper")
                    .raw_value()),
            "installing enumerate helper");
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_enumerate_source"),
                unwrap_bootstrap_expected(vm,
                                          make_intrinsic_function(vm, source),
                                          "creating enumerate helper")
                    .raw_value()),
            "installing enumerate helper");
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_enumerate_take_index"),
                unwrap_bootstrap_expected(
                    vm, make_intrinsic_function(vm, take_index),
                    "creating enumerate helper")
                    .raw_value()),
            "installing enumerate helper");
    }

    void install_enumerate_python_methods(VirtualMachine *vm,
                                          ModuleObject *module)
    {
        const wchar_t *methods[] = {L"__new__", L"__next__"};
        const wchar_t *helpers[] = {L"_clover_enumerate_new",
                                    L"_clover_enumerate_next"};
        DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly) |
                                descriptor_flag(DescriptorFlag::StableSlot);
        for(size_t idx = 0; idx < std::size(methods); ++idx)
        {
            Value function = module->get_own_property(
                vm->get_or_create_interned_string_value(helpers[idx]));
            if(!has_native_layout<Function>(function))
                fatal("trusted builtin source did not define enumerate method");
            bool stored = vm->enumerate_class()->define_own_property(
                vm->get_or_create_interned_string_value(methods[idx]), function,
                flags);
            assert(stored);
            (void)stored;
        }
    }
}  // namespace cl
