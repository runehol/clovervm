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

    // Size errors retain the source and remain sticky. Exhaustion and keys
    // errors detach it, so neither can be revived by later dictionary changes.
    template <typename Iterator>
    [[nodiscard]] static Expected<Dict::EntryView>
    next_entry(ThreadState *thread, TValue<Iterator> iterator)
    {
        Iterator *state = iterator.extract();
        if(!state->dict.value())
        {
            (void)thread->set_pending_stop_iteration_no_value();
            return Expected<Dict::EntryView>::propagate_exception();
        }
        Dict *dict = state->dict.value().value().extract();
        if(static_cast<int64_t>(dict->size()) != state->expected_size.extract())
        {
            state->expected_size = TValue<SMI>::from_smi(-1);
            return Expected<Dict::EntryView>::raise_exception(
                L"RuntimeError", L"dictionary changed size during iteration");
        }
        size_t index = static_cast<size_t>(state->index.extract());
        Dict::EntryView entry;
        while(index < dict->entry_storage_size())
        {
            if(dict->entry_at(index, entry))
            {
                if(state->remaining.extract() == 0)
                {
                    state->dict = Optional<TValue<Dict>>::none();
                    return Expected<Dict::EntryView>::raise_exception(
                        L"RuntimeError",
                        L"dictionary keys changed during iteration");
                }
                state->index =
                    TValue<SMI>::from_smi(static_cast<int64_t>(index + 1));
                state->remaining =
                    TValue<SMI>::from_smi(state->remaining.extract() - 1);
                return Expected<Dict::EntryView>::ok(entry);
            }
            ++index;
        }
        state->dict = Optional<TValue<Dict>>::none();
        (void)thread->set_pending_stop_iteration_no_value();
        return Expected<Dict::EntryView>::propagate_exception();
    }

    template <typename Iterator>
    [[nodiscard]] static Value
    native_dict_iterator_length_hint(ThreadState *thread, Value self)
    {
        TValue<Iterator> iterator =
            CL_TRY(TValue<Iterator>::from_value_or_raise(
                self, L"TypeError",
                L"dictionary iterator.__length_hint__ expects a matching "
                L"iterator receiver"));
        Iterator *state = iterator.extract();
        if(!state->dict.value() ||
           static_cast<int64_t>(
               state->dict.value().value().extract()->size()) !=
               state->expected_size.extract())
        {
            return Value::from_smi(0);
        }
        return state->remaining.raw_value();
    }

    [[nodiscard]] static Value
    native_dict_key_iterator_next(ThreadState *thread, Value self)
    {
        TValue<DictKeyIterator> iterator =
            CL_TRY(TValue<DictKeyIterator>::from_value_or_raise(
                self, L"TypeError",
                L"dict_keyiterator.__next__ expects a dict_keyiterator "
                L"receiver"));
        return CL_TRY(next_entry(thread, iterator)).key;
    }

    [[nodiscard]] static Value
    native_dict_value_iterator_next(ThreadState *thread, Value self)
    {
        TValue<DictValueIterator> iterator =
            CL_TRY(TValue<DictValueIterator>::from_value_or_raise(
                self, L"TypeError",
                L"dict_valueiterator.__next__ expects a dict_valueiterator "
                L"receiver"));
        return CL_TRY(next_entry(thread, iterator)).value;
    }

    [[nodiscard]] static Value
    native_dict_item_iterator_next(ThreadState *thread, Value self)
    {
        TValue<DictItemIterator> iterator =
            CL_TRY(TValue<DictItemIterator>::from_value_or_raise(
                self, L"TypeError",
                L"dict_itemiterator.__next__ expects a dict_itemiterator "
                L"receiver"));
        Dict::EntryView entry = CL_TRY(next_entry(thread, iterator));
        Owned<Value> key(entry.key);
        Owned<Value> value(entry.value);
        TValue<Tuple> item = make_object_value<Tuple>(2);
        item.extract()->initialize_item_unchecked(0, key.value());
        item.extract()->initialize_item_unchecked(1, value.value());
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
            vm->get_or_create_interned_string_value(L"dict_keys"), 0, methods,
            std::size(methods), vm->object_class(), SubclassPolicy::Disallowed);
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
            vm->get_or_create_interned_string_value(L"dict_values"), 0, methods,
            std::size(methods), vm->object_class(), SubclassPolicy::Disallowed);
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
            vm->get_or_create_interned_string_value(L"dict_items"), 0, methods,
            std::size(methods), vm->object_class(), SubclassPolicy::Disallowed);
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
            {vm->get_or_create_interned_string_value(L"__length_hint__"),
             unwrap_bootstrap_expected(
                 vm,
                 make_intrinsic_function(
                     vm, native_dict_iterator_length_hint<DictKeyIterator>),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictKeyIterator>(
            vm->get_or_create_interned_string_value(L"dict_keyiterator"), 0,
            methods, std::size(methods), vm->object_class(),
            SubclassPolicy::Disallowed);
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
            {vm->get_or_create_interned_string_value(L"__length_hint__"),
             unwrap_bootstrap_expected(
                 vm,
                 make_intrinsic_function(
                     vm, native_dict_iterator_length_hint<DictValueIterator>),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictValueIterator>(
            vm->get_or_create_interned_string_value(L"dict_valueiterator"), 0,
            methods, std::size(methods), vm->object_class(),
            SubclassPolicy::Disallowed);
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
            {vm->get_or_create_interned_string_value(L"__length_hint__"),
             unwrap_bootstrap_expected(
                 vm,
                 make_intrinsic_function(
                     vm, native_dict_iterator_length_hint<DictItemIterator>),
                 "creating intrinsic function")
                 .raw_value()},
        };
        ClassObject *cls = ClassObject::make_builtin_class<DictItemIterator>(
            vm->get_or_create_interned_string_value(L"dict_itemiterator"), 0,
            methods, std::size(methods), vm->object_class(),
            SubclassPolicy::Disallowed);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

}  // namespace cl
