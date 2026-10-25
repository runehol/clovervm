#include "builtin_types/sequence_iterator.h"
#include "builtin_types/bigint.h"
#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "runtime/fatal.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cstddef>
#include <iterator>
#include <limits>
#include <string>

namespace cl
{
    static constexpr int64_t max_index = std::numeric_limits<ptrdiff_t>::max();

    // Inspection never binds or executes a descriptor. In particular, presence
    // of __next__ is sufficient even when the stored attribute is None.
    static AttributeReadDescriptor special(Value obj, const wchar_t *name)
    {
        return resolve_special_method_read_descriptor(obj,
                                                      interned_string(name));
    }

    [[nodiscard]] static Value type_error(ThreadState *thread, Value obj,
                                          const wchar_t *prefix,
                                          const wchar_t *suffix)
    {
        TValue<String> name = thread->class_of_value(obj)->get_name();
        std::wstring message(prefix);
        message.append(name.extract()->data,
                       static_cast<size_t>(name.extract()->count.extract()));
        message.append(suffix);
        return thread->set_pending_builtin_exception_string(L"TypeError",
                                                            message.c_str());
    }

    [[nodiscard]] static Value native_iter_kind(ThreadState *thread, Value obj)
    {
        AttributeReadDescriptor descriptor = special(obj, L"__iter__");
        if(descriptor.is_found())
        {
            if(descriptor.lookup_value.is_none())
                return type_error(thread, obj, L"'",
                                  L"' object is not iterable");
            return Value::True();
        }
        if(!special(obj, L"__getitem__").is_found())
            return type_error(thread, obj, L"'", L"' object is not iterable");
        return Value::False();
    }

    [[nodiscard]] static Value native_validate_iterator(ThreadState *thread,
                                                        Value obj)
    {
        if(!special(obj, L"__next__").is_found())
            return type_error(thread, obj,
                              L"iter() returned non-iterator of type '", L"'");
        return obj;
    }

    [[nodiscard]] static Value native_sequence_new(ThreadState *thread,
                                                   Value obj)
    {
        return thread->make_object_value<SequenceIterator>(obj).raw_value();
    }

    [[nodiscard]] static Expected<TValue<SequenceIterator>> receiver(Value self)
    {
        return TValue<SequenceIterator>::from_value_or_raise(
            self, L"TypeError",
            L"iterator.__next__ expects a sequence iterator receiver");
    }

    [[nodiscard]] static Value native_sequence_iter(ThreadState *thread,
                                                    Value self)
    {
        (void)thread;
        (void)CL_TRY(TValue<SequenceIterator>::from_value_or_raise(
            self, L"TypeError",
            L"iterator.__iter__ expects a sequence iterator receiver"));
        return self;
    }

    [[nodiscard]] static Value native_sequence_source(ThreadState *thread,
                                                      Value self)
    {
        TValue<SequenceIterator> iterator = CL_TRY(receiver(self));
        Value source = iterator.extract()->source;
        if(source.is_not_present())
            return thread->set_pending_stop_iteration_no_value();
        return source;
    }

    [[nodiscard]] static Value native_sequence_index(ThreadState *thread,
                                                     Value self)
    {
        TValue<SequenceIterator> iterator = CL_TRY(receiver(self));
        int64_t index = iterator.extract()->index;
        if(index == max_index)
            return thread->set_pending_builtin_exception_string(
                L"OverflowError", L"iter index too large");
        return CL_TRY(int_value_from_int64(thread, index));
    }

    [[nodiscard]] static Value native_sequence_advance(ThreadState *thread,
                                                       Value self)
    {
        (void)thread;
        TValue<SequenceIterator> iterator = CL_TRY(receiver(self));
        // Reentrant callbacks can advance or exhaust this same iterator. Use
        // current state, never restore a captured index or source after a call.
        if(iterator.extract()->index < max_index)
            ++iterator.extract()->index;
        return Value::None();
    }

    [[nodiscard]] static Value native_sequence_clear(ThreadState *thread,
                                                     Value self)
    {
        (void)thread;
        TValue<SequenceIterator> iterator = CL_TRY(receiver(self));
        iterator.extract()->source = Value::not_present();
        return Value::None();
    }

    BuiltinClassDefinition make_sequence_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId ids[] = {
            NativeLayoutId::SequenceIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_sequence_iter),
                 "creating sequence iterator iter")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<SequenceIterator>(
            vm->get_or_create_interned_string_value(L"iterator"), 0, methods,
            std::size(methods), vm->object_class(), SubclassPolicy::Disallowed);
        return builtin_class_definition(cls, ids, BuiltinsVisibility::Internal);
    }

    void install_sequence_iterator_bindings(VirtualMachine *vm,
                                            ModuleObject *module)
    {
        auto bind = [&](const wchar_t *name, auto function) {
            Value value = unwrap_bootstrap_expected(
                              vm, make_intrinsic_function(vm, function),
                              "creating iteration helper")
                              .raw_value();
            (void)unwrap_bootstrap_expected(
                vm, module->set_own_property(interned_string(name), value),
                "installing iteration helper");
        };
        bind(L"_clover_iter_kind", native_iter_kind);
        bind(L"_clover_validate_iterator", native_validate_iterator);
        bind(L"_clover_sequence_new", native_sequence_new);
        bind(L"_clover_sequence_source", native_sequence_source);
        bind(L"_clover_sequence_index", native_sequence_index);
        bind(L"_clover_sequence_advance", native_sequence_advance);
        bind(L"_clover_sequence_clear", native_sequence_clear);
    }

    void install_sequence_iterator_python_methods(VirtualMachine *vm,
                                                  ModuleObject *module)
    {
        Value function = module->get_own_property(
            interned_string(L"_clover_sequence_iterator_next"));
        if(!has_native_layout<Function>(function))
            fatal("missing sequence iterator next helper");
        DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly) |
                                descriptor_flag(DescriptorFlag::StableSlot);
        bool stored =
            vm->class_for_native_layout(NativeLayoutId::SequenceIterator)
                ->define_own_property(interned_string(L"__next__"), function,
                                      flags);
        assert(stored);
        (void)stored;
    }
}  // namespace cl
