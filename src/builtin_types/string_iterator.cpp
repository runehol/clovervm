#include "builtin_types/string_iterator.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

namespace cl
{
    [[nodiscard]] static Value native_string_iterator_iter(ThreadState *thread,
                                                           Value self)
    {
        (void)CL_TRY(TValue<StringIterator>::from_value_or_raise(
            self, L"TypeError",
            L"str_iterator.__iter__ expects a str_iterator receiver"));
        return self;
    }

    [[nodiscard]] static Value native_string_iterator_next(ThreadState *thread,
                                                           Value self)
    {
        TValue<StringIterator> iterator_value =
            CL_TRY(TValue<StringIterator>::from_value_or_raise(
                self, L"TypeError",
                L"str_iterator.__next__ expects a str_iterator receiver"));
        StringIterator *iterator = iterator_value.extract();
        int64_t index_smi = iterator->index.extract();
        int64_t length_smi = iterator->string.extract()->count.extract();
        assert(index_smi >= 0);
        assert(length_smi >= 0);
        size_t index = static_cast<size_t>(index_smi);
        if(index >= static_cast<size_t>(length_smi))
        {
            return active_thread()->set_pending_stop_iteration_no_value();
        }

        Value character = iterator->string.extract()->char_at(
            thread, static_cast<int64_t>(index));
        CL_PROPAGATE_EXCEPTION(character);
        iterator->index =
            TValue<SMI>::from_smi(static_cast<int64_t>(index + 1));
        return character;
    }

    BuiltinClassDefinition make_string_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::StringIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_string_iterator_iter),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__next__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_string_iterator_next),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<StringIterator>(
            vm->get_or_create_interned_string_value(L"str_iterator"), 0,
            methods, std::size(methods), vm->object_class(),
            SubclassPolicy::Disallowed);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

}  // namespace cl
