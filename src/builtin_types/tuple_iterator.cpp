#include "builtin_types/tuple_iterator.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

namespace cl
{
    static Value native_tuple_iterator_iter(ThreadState *thread, Value self)
    {
        (void)CL_TRY(TValue<TupleIterator>::from_value_or_raise(
            self, L"TypeError",
            L"tuple_iterator.__iter__ expects a tuple_iterator receiver"));
        return self;
    }

    static Value native_tuple_iterator_next(ThreadState *thread, Value self)
    {
        TValue<TupleIterator> iterator_value =
            CL_TRY(TValue<TupleIterator>::from_value_or_raise(
                self, L"TypeError",
                L"tuple_iterator.__next__ expects a tuple_iterator receiver"));
        TupleIterator *iterator = iterator_value.extract();
        int64_t index_smi = iterator->index.extract();
        int64_t length_smi = iterator->length.extract();
        assert(index_smi >= 0);
        assert(length_smi >= 0);
        size_t index = static_cast<size_t>(index_smi);
        if(index >= static_cast<size_t>(length_smi))
        {
            return active_thread()->set_pending_stop_iteration_no_value();
        }

        iterator->index =
            TValue<SMI>::from_smi(static_cast<int64_t>(index + 1));
        Tuple *tuple = iterator->tuple.extract();
        return tuple->item_unchecked(index);
    }

    BuiltinClassDefinition make_tuple_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::TupleIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_tuple_iterator_iter),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__next__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_tuple_iterator_next),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<TupleIterator>(
            vm->get_or_create_interned_string_value(L"tuple_iterator"),
            TupleIterator::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

}  // namespace cl
