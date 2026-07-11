#include "builtin_types/dict_view.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <iterator>

namespace cl
{
    static Value native_dict_keys_view_len(ThreadState *thread, Value self)
    {
        TValue<DictKeysView> view =
            CL_TRY(TValue<DictKeysView>::from_value_or_raise(
                self, L"TypeError",
                L"dict_keys.__len__ expects a dict_keys receiver"));
        return Value::from_smi(
            static_cast<int64_t>(view.extract()->dict.extract()->size()));
    }

    static Value native_dict_values_view_len(ThreadState *thread, Value self)
    {
        TValue<DictValuesView> view =
            CL_TRY(TValue<DictValuesView>::from_value_or_raise(
                self, L"TypeError",
                L"dict_values.__len__ expects a dict_values receiver"));
        return Value::from_smi(
            static_cast<int64_t>(view.extract()->dict.extract()->size()));
    }

    static Value native_dict_items_view_len(ThreadState *thread, Value self)
    {
        TValue<DictItemsView> view =
            CL_TRY(TValue<DictItemsView>::from_value_or_raise(
                self, L"TypeError",
                L"dict_items.__len__ expects a dict_items receiver"));
        return Value::from_smi(
            static_cast<int64_t>(view.extract()->dict.extract()->size()));
    }

    static Value native_dict_keys_view_iter(ThreadState *thread, Value self)
    {
        TValue<DictKeysView> view =
            CL_TRY(TValue<DictKeysView>::from_value_or_raise(
                self, L"TypeError",
                L"dict_keys.__iter__ expects a dict_keys receiver"));
        return make_object_value<DictKeyIterator>(view.extract()->dict)
            .raw_value();
    }

    static Value native_dict_values_view_iter(ThreadState *thread, Value self)
    {
        TValue<DictValuesView> view =
            CL_TRY(TValue<DictValuesView>::from_value_or_raise(
                self, L"TypeError",
                L"dict_values.__iter__ expects a dict_values receiver"));
        return make_object_value<DictValueIterator>(view.extract()->dict)
            .raw_value();
    }

    static Value native_dict_items_view_iter(ThreadState *thread, Value self)
    {
        TValue<DictItemsView> view =
            CL_TRY(TValue<DictItemsView>::from_value_or_raise(
                self, L"TypeError",
                L"dict_items.__iter__ expects a dict_items receiver"));
        return make_object_value<DictItemIterator>(view.extract()->dict)
            .raw_value();
    }

    static Value native_dict_key_iterator_iter(ThreadState *thread, Value self)
    {
        (void)CL_TRY(TValue<DictKeyIterator>::from_value_or_raise(
            self, L"TypeError",
            L"dict_keyiterator.__iter__ expects a dict_keyiterator receiver"));
        return self;
    }

    static Value native_dict_value_iterator_iter(ThreadState *thread,
                                                 Value self)
    {
        (void)CL_TRY(TValue<DictValueIterator>::from_value_or_raise(
            self, L"TypeError",
            L"dict_valueiterator.__iter__ expects a dict_valueiterator "
            L"receiver"));
        return self;
    }

    static Value native_dict_item_iterator_iter(ThreadState *thread, Value self)
    {
        (void)CL_TRY(TValue<DictItemIterator>::from_value_or_raise(
            self, L"TypeError",
            L"dict_itemiterator.__iter__ expects a dict_itemiterator "
            L"receiver"));
        return self;
    }

    static Value check_expected_size(Dict *dict,
                                     Member<TValue<SMI>> &expected_size)
    {
        if(static_cast<int64_t>(dict->size()) != expected_size.extract())
        {
            expected_size = TValue<SMI>::from_smi(-1);
            return active_thread()->set_pending_builtin_exception_string(
                L"RuntimeError", L"dictionary changed size during iteration");
        }
        return Value::None();
    }

    static bool next_entry(Dict *dict, Member<TValue<SMI>> &index,
                           Dict::EntryView &entry)
    {
        int64_t index_smi = index.extract();
        assert(index_smi >= 0);
        size_t scan_idx = static_cast<size_t>(index_smi);
        while(scan_idx < dict->entry_storage_size())
        {
            if(dict->entry_at(scan_idx, entry))
            {
                index =
                    TValue<SMI>::from_smi(static_cast<int64_t>(scan_idx + 1));
                return true;
            }
            ++scan_idx;
        }
        index = TValue<SMI>::from_smi(static_cast<int64_t>(scan_idx));
        return false;
    }

    static Value native_dict_key_iterator_next(ThreadState *thread, Value self)
    {
        TValue<DictKeyIterator> iterator =
            CL_TRY(TValue<DictKeyIterator>::from_value_or_raise(
                self, L"TypeError",
                L"dict_keyiterator.__next__ expects a dict_keyiterator "
                L"receiver"));
        Dict *dict = iterator.extract()->dict.extract();
        CL_PROPAGATE_EXCEPTION(
            check_expected_size(dict, iterator.extract()->expected_size));
        Dict::EntryView entry;
        if(!next_entry(dict, iterator.extract()->index, entry))
        {
            return active_thread()->set_pending_stop_iteration_no_value();
        }
        return entry.key;
    }

    static Value native_dict_value_iterator_next(ThreadState *thread,
                                                 Value self)
    {
        TValue<DictValueIterator> iterator =
            CL_TRY(TValue<DictValueIterator>::from_value_or_raise(
                self, L"TypeError",
                L"dict_valueiterator.__next__ expects a dict_valueiterator "
                L"receiver"));
        Dict *dict = iterator.extract()->dict.extract();
        CL_PROPAGATE_EXCEPTION(
            check_expected_size(dict, iterator.extract()->expected_size));
        Dict::EntryView entry;
        if(!next_entry(dict, iterator.extract()->index, entry))
        {
            return active_thread()->set_pending_stop_iteration_no_value();
        }
        return entry.value;
    }

    static Value native_dict_item_iterator_next(ThreadState *thread, Value self)
    {
        TValue<DictItemIterator> iterator =
            CL_TRY(TValue<DictItemIterator>::from_value_or_raise(
                self, L"TypeError",
                L"dict_itemiterator.__next__ expects a dict_itemiterator "
                L"receiver"));
        Dict *dict = iterator.extract()->dict.extract();
        CL_PROPAGATE_EXCEPTION(
            check_expected_size(dict, iterator.extract()->expected_size));
        Dict::EntryView entry;
        if(!next_entry(dict, iterator.extract()->index, entry))
        {
            return active_thread()->set_pending_stop_iteration_no_value();
        }
        TValue<Tuple> item = make_object_value<Tuple>(2);
        item.extract()->initialize_item_unchecked(0, entry.key);
        item.extract()->initialize_item_unchecked(1, entry.value);
        return item.raw_value();
    }

    BuiltinClassDefinition make_dict_keys_view_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::DictKeysView};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__len__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_keys_view_len),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_keys_view_iter),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictKeysView>(
            vm->get_or_create_interned_string_value(L"dict_keys"),
            DictKeysView::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

    BuiltinClassDefinition make_dict_values_view_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::DictValuesView};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__len__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_values_view_len),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_values_view_iter),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictValuesView>(
            vm->get_or_create_interned_string_value(L"dict_values"),
            DictValuesView::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

    BuiltinClassDefinition make_dict_items_view_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::DictItemsView};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__len__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_items_view_len),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_items_view_iter),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictItemsView>(
            vm->get_or_create_interned_string_value(L"dict_items"),
            DictItemsView::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

    BuiltinClassDefinition make_dict_key_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::DictKeyIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_key_iterator_iter),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__next__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, native_dict_key_iterator_next),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictKeyIterator>(
            vm->get_or_create_interned_string_value(L"dict_keyiterator"),
            DictKeyIterator::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

    BuiltinClassDefinition make_dict_value_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::DictValueIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm,
                 make_intrinsic_function(vm, native_dict_value_iterator_iter),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__next__"),
             unwrap_bootstrap_expected(
                 vm,
                 make_intrinsic_function(vm, native_dict_value_iterator_next),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictValueIterator>(
            vm->get_or_create_interned_string_value(L"dict_valueiterator"),
            DictValueIterator::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

    BuiltinClassDefinition make_dict_item_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::DictItemIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm,
                 make_intrinsic_function(vm, native_dict_item_iterator_iter),
                 "creating intrinsic function")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__next__"),
             unwrap_bootstrap_expected(
                 vm,
                 make_intrinsic_function(vm, native_dict_item_iterator_next),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictItemIterator>(
            vm->get_or_create_interned_string_value(L"dict_itemiterator"),
            DictItemIterator::native_static_release_count(), methods,
            std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

}  // namespace cl
