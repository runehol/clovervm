#include "list_iterator.h"
#include "class_object.h"
#include "native_function.h"
#include "thread_state.h"
#include "virtual_machine.h"

namespace cl
{
    static Value native_list_iterator_iter(Value self)
    {
        (void)CL_TRY(TValue2<ListIterator>::from_value_or_raise(
            self, L"TypeError",
            L"list_iterator.__iter__ expects a list_iterator receiver"));
        return self;
    }

    static Value native_list_iterator_next(Value self)
    {
        TValue2<ListIterator> iterator_value =
            CL_TRY(TValue2<ListIterator>::from_value_or_raise(
                self, L"TypeError",
                L"list_iterator.__next__ expects a list_iterator receiver"));
        ListIterator *iterator = iterator_value.extract();
        int64_t index_smi = iterator->index.extract();
        assert(index_smi >= 0);
        size_t index = static_cast<size_t>(index_smi);
        List *list = iterator->list.extract();
        if(index >= list->size())
        {
            return active_thread()->set_pending_stop_iteration_no_value();
        }

        iterator->index =
            TValue2<SMI>::from_smi(static_cast<int64_t>(index + 1));
        return list->item_unchecked(index);
    }

    BuiltinClassDefinition make_list_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ListIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             make_native_function(vm, native_list_iterator_iter).raw_value()},
            {vm->get_or_create_interned_string_value(L"__next__"),
             make_native_function(vm, native_list_iterator_next).raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"list_iterator"),
            ListIterator::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }

}  // namespace cl
