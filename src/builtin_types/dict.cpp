#include "builtin_types/dict.h"
#include "builtin_types/dict_view.h"
#include "builtin_types/list.h"
#include "builtin_types/str.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/owned.h"
#include "object_model/refcount.h"
#include "object_model/shape.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cassert>
#include <iterator>

namespace cl
{

    /*
      TODO: these just assume string keys. replace with full equality machinery
      when we have calling Python-defined methods from C++ up and running */

    static TValue<SMI> internal_hash(TValue<String> key)
    {
        return string_hash_normalized(key);
    }

    static bool internal_eq(TValue<String> a, TValue<String> b)
    {
        return string_eq(a, b);
    }

    static bool is_exact_dict_string_key_shape(ThreadState *thread,
                                               const Dict *dict)
    {
        return dict->get_shape() == thread->get_exact_dict_string_key_shape();
    }

    Dict::Dict(ClassObject *cls)
        : Object(cls, native_layout), hash_table(min_table_size, not_present),
          n_valid_entries(0), table_generation_(0)
    {
    }

    Dict::Dict(ClassObject *cls, const Dict &other)
        : Object(cls, native_layout), hash_table(min_table_size, not_present),
          n_valid_entries(0), table_generation_(0)
    {
        ThreadState *thread = active_thread();
        if(!is_exact_dict_string_key_shape(thread, &other))
        {
            promote_to_general_shape(thread);
        }
        for(const Entry &e: other.entries)
        {
            if(e.valid())
            {
                copy_stored_entry(e.key, e.value, e.hash);
            }
        }
    }

    BuiltinClassDefinition make_dict_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Dict};
        ClassObject *cls = ClassObject::make_builtin_class<Dict>(
            vm->get_or_create_interned_string_value(L"dict"),
            Dict::native_static_release_count(), nullptr, 0,
            vm->object_class());
        Shape *string_key_shape = cls->get_instance_root_shape();
        Shape *general_shape =
            string_key_shape->clone_with_flags(string_key_shape->flags());
        vm->install_exact_dict_shapes(cls, string_key_shape, general_shape);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    static Value native_dict_new(ThreadState *thread, Value cls_value)
    {
        if(cls_value != Value::from_oop(thread->get_machine()->dict_class()))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"dict.__new__ expects dict as cls");
        }
        return thread->make_object_value<Dict>().raw_value();
    }

    static Value native_dict_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Dict>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict.__repr__ expects a dict receiver");
        }

        Dict *dict = self.get_ptr<Dict>();
        StringBuilder builder;
        builder.append_char(L'{');
        bool first = true;
        for(Dict::EntryView entry: *dict)
        {
            if(!first)
            {
                builder.append_c_str(L", ");
            }
            first = false;
            CL_PROPAGATE_EXCEPTION(builder.append_repr(entry.key));
            builder.append_c_str(L": ");
            CL_PROPAGATE_EXCEPTION(builder.append_repr(entry.value));
        }
        builder.append_char(L'}');
        return builder.finish();
    }

    static Value native_dict_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Dict>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict.__len__ expects a dict receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<Dict>()->size()));
    }

    static Value require_dict_receiver(Value self, const wchar_t *method_name)
    {
        if(!can_convert_to<Dict>(self))
        {
            std::wstring message = L"dict.";
            message += method_name;
            message += L" expects a dict receiver";
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        return Value::None();
    }

    static Value native_dict_clear(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"clear"));
        self.get_ptr<Dict>()->clear();
        return Value::None();
    }

    static Value native_dict_copy(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"copy"));
        return self.get_ptr<Dict>()->copy().raw_value();
    }

    static Value native_dict_get(ThreadState *thread, Value self, Value key,
                                 Value default_value)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"get"));
        return CL_TRY(self.get_ptr<Dict>()->get_item_or_default(thread, key,
                                                                default_value));
    }

    static Value native_dict_getitem(ThreadState *thread, Value self, Value key)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"__getitem__"));
        return CL_TRY(self.get_ptr<Dict>()->get_item(thread, key));
    }

    static Value native_dict_contains(ThreadState *thread, Value self,
                                      Value key)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"__contains__"));
        return CL_TRY(self.get_ptr<Dict>()->contains(thread, key))
                   ? Value::True()
                   : Value::False();
    }

    static Value native_dict_setitem(ThreadState *thread, Value self, Value key,
                                     Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"__setitem__"));
        CL_TRY(self.get_ptr<Dict>()->set_item(thread, key, value));
        return Value::None();
    }

    static Value native_dict_delitem(ThreadState *thread, Value self, Value key)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"__delitem__"));
        CL_TRY(self.get_ptr<Dict>()->del_item(thread, key));
        return Value::None();
    }

    static Value trusted_dict_getitem_str_handler(ThreadState *thread,
                                                  Value self, Value key)
    {
        return CL_TRY(self.get_ptr<Dict>()->get_item_for_str(
            thread, TValue<String>::from_value_unchecked(key)));
    }

    static Value trusted_dict_setitem_str_handler(ThreadState *thread,
                                                  Value self, Value key,
                                                  Value value)
    {
        CL_TRY(self.get_ptr<Dict>()->set_item_for_str(
            thread, TValue<String>::from_value_unchecked(key), value));
        return Value::None();
    }

    static Value trusted_dict_delitem_str_handler(ThreadState *thread,
                                                  Value self, Value key)
    {
        CL_TRY(self.get_ptr<Dict>()->del_item_for_str(
            thread, TValue<String>::from_value_unchecked(key)));
        return Value::None();
    }

    static Value trusted_dict_contains_handler(ThreadState *thread, Value self,
                                               Value key)
    {
        if(!can_convert_to<String>(key))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"dict keys must be str");
        }
        return CL_TRY(self.get_ptr<Dict>()->contains_for_str(
                   thread, TValue<String>::from_value_unchecked(key)))
                   ? Value::True()
                   : Value::False();
    }

    static bool trusted_dict_str_key_shapes_match(VirtualMachine *vm,
                                                  ShapeKey container_key,
                                                  ShapeKey key_key)
    {
        return vm->shape_for_key(container_key) ==
                   vm->exact_dict_string_key_shape() &&
               vm->shape_for_key(key_key)->get_class() == vm->str_class();
    }

    static TrustedResolution resolve_trusted_dict_getitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_getitem_str_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_dict_contains_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_contains_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_dict_setitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Ternary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_setitem_str_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_dict_delitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_delitem_str_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Value native_dict_keys(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"keys"));
        return self.get_ptr<Dict>()->keys();
    }

    static Value native_dict_values(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"values"));
        return self.get_ptr<Dict>()->values();
    }

    static Value native_dict_items(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"items"));
        return self.get_ptr<Dict>()->items();
    }

    static Value native_dict_pop(ThreadState *thread, Value self, Value key)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"pop"));
        return CL_TRY(self.get_ptr<Dict>()->pop(thread, key));
    }

    static Value native_dict_popitem(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"popitem"));
        return self.get_ptr<Dict>()->popitem();
    }

    static Value native_dict_setdefault(ThreadState *thread, Value self,
                                        Value key, Value default_value)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"setdefault"));
        return CL_TRY(
            self.get_ptr<Dict>()->setdefault(thread, key, default_value));
    }

    static Value native_dict_update(ThreadState *thread, Value self,
                                    Value other)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"update"));
        if(other == Value::None())
        {
            return Value::None();
        }
        if(!can_convert_to<Dict>(other))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict.update expects a dict argument");
        }

        CL_TRY(self.get_ptr<Dict>()->update_from_dict(thread,
                                                      other.get_ptr<Dict>()));
        return Value::None();
    }

    static Value native_dict_fromkeys(ThreadState *thread, Value keys,
                                      Value value)
    {
        if(can_convert_to<Tuple>(keys))
        {
            Tuple *tuple = keys.get_ptr<Tuple>();
            return Dict::from_tuple_keys(tuple, value);
        }
        if(can_convert_to<List>(keys))
        {
            List *list = keys.get_ptr<List>();
            return Dict::from_list_keys(list, value);
        }

        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"dict.fromkeys expects a tuple or list");
    }

    static TValue<Tuple> make_single_default(VirtualMachine *vm, Value value)
    {
        TValue<Tuple> defaults =
            vm->get_default_thread()->make_object_value<Tuple>(1);
        defaults.extract()->initialize_item_unchecked(0, value);
        return defaults;
    }

    void install_dict_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__new__", native_dict_new,
                                     L"Create a dict object."),
            builtin_intrinsic_method(L"__str__", native_dict_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_dict_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_dict_len,
                                     L"Return len(self)."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->dict_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");

        // TODO: Move this back to install_builtin_intrinsic_methods when it
        // supports default parameters directly.
        ClassObject *cls = vm->dict_class();
        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeFlags class_shape_flags = cls->get_shape()->flags();
        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags & ~fixed_attribute_shape_flags()));

        auto install = [&](const wchar_t *name, auto function,
                           Optional<TValue<Tuple>> defaults =
                               Optional<TValue<Tuple>>::none()) {
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(name),
                unwrap_bootstrap_expected(
                    vm, make_intrinsic_function(vm, function, defaults),
                    "creating intrinsic function")
                    .raw_value(),
                method_flags);
            assert(stored);
            (void)stored;
        };
        install(L"clear", native_dict_clear);
        install(L"copy", native_dict_copy);

        auto install_trusted = [&](const wchar_t *name, auto function,
                                   TrustedHandlerResolver resolver) {
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(name),
                unwrap_bootstrap_expected(
                    vm,
                    make_intrinsic_function(
                        vm, with_trusted_handler_resolver(
                                builtin_intrinsic_method(name, function),
                                resolver)),
                    "creating intrinsic function")
                    .raw_value(),
                method_flags);
            assert(stored);
            (void)stored;
        };
        install_trusted(L"__getitem__", native_dict_getitem,
                        resolve_trusted_dict_getitem_handler);
        install_trusted(L"__setitem__", native_dict_setitem,
                        resolve_trusted_dict_setitem_handler);
        install_trusted(L"__delitem__", native_dict_delitem,
                        resolve_trusted_dict_delitem_handler);

        install_trusted(L"__contains__", native_dict_contains,
                        resolve_trusted_dict_contains_handler);
        install(L"get", native_dict_get,
                Optional<TValue<Tuple>>::some(
                    make_single_default(vm, Value::None())));
        install(L"keys", native_dict_keys);
        install(L"values", native_dict_values);
        install(L"items", native_dict_items);
        install(L"pop", native_dict_pop);
        install(L"popitem", native_dict_popitem);
        install(L"setdefault", native_dict_setdefault,
                Optional<TValue<Tuple>>::some(
                    make_single_default(vm, Value::None())));
        install(L"update", native_dict_update,
                Optional<TValue<Tuple>>::some(
                    make_single_default(vm, Value::None())));
        install(L"fromkeys", native_dict_fromkeys,
                Optional<TValue<Tuple>>::some(
                    make_single_default(vm, Value::None())));

        cls->set_shape(cls->get_shape()->clone_with_flags(class_shape_flags));
    }

    TValue<Dict> Dict::copy() const { return make_object_value<Dict>(*this); }

    Value Dict::keys()
    {
        return make_object_value<DictKeysView>(TValue<Dict>::from_oop(this))
            .raw_value();
    }

    Value Dict::values()
    {
        return make_object_value<DictValuesView>(TValue<Dict>::from_oop(this))
            .raw_value();
    }

    Value Dict::items()
    {
        return make_object_value<DictItemsView>(TValue<Dict>::from_oop(this))
            .raw_value();
    }

    Value Dict::popitem()
    {
        if(empty())
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"KeyError");
        }

        int32_t last_entry_idx = -1;
        for(size_t entry_idx = 0; entry_idx < entries.size(); ++entry_idx)
        {
            if(entries[entry_idx].valid())
            {
                last_entry_idx = static_cast<int32_t>(entry_idx);
            }
        }
        assert(last_entry_idx >= 0);
        Entry last = entries[last_entry_idx];
        int64_t last_hash_idx = -1;
        for(size_t hash_idx = 0; hash_idx < hash_table.size(); ++hash_idx)
        {
            if(hash_table[hash_idx] == last_entry_idx)
            {
                last_hash_idx = static_cast<int64_t>(hash_idx);
                break;
            }
        }
        assert(last_hash_idx >= 0);
        Owned<Value> last_key(last.key);
        Owned<Value> last_value(last.value);
        delete_entry_at_slot(static_cast<size_t>(last_hash_idx));
        Owned<TValue<Tuple>> result(make_object_value<Tuple>(2));
        result.extract()->initialize_item_unchecked(0, last_key.value());
        result.extract()->initialize_item_unchecked(1, last_value.value());
        return result.raw_value();
    }

    Expected<void> Dict::update_from_dict(ThreadState *thread,
                                          const Dict *other)
    {
        for(size_t idx = 0; idx < other->entries.size(); ++idx)
        {
            Entry entry = other->entries[idx];
            if(!entry.valid())
            {
                continue;
            }
            CL_TRY(set_item_with_known_hash(thread, entry.key, entry.value,
                                            entry.hash));
        }
        return Expected<void>::ok();
    }

    Value Dict::from_tuple_keys(const Tuple *keys, Value value)
    {
        ThreadState *thread = active_thread();
        Owned<TValue<Dict>> result(make_object_value<Dict>());
        Owned<Value> live_value(value);
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            CL_TRY(result.extract()->set_item(thread, keys->item_unchecked(idx),
                                              live_value.value()));
        }
        return result.raw_value();
    }

    Value Dict::from_list_keys(const List *keys, Value value)
    {
        ThreadState *thread = active_thread();
        Owned<TValue<Dict>> result(make_object_value<Dict>());
        Owned<Value> live_value(value);
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            CL_TRY(result.extract()->set_item(thread, keys->item_unchecked(idx),
                                              live_value.value()));
        }
        return result.raw_value();
    }

    Dict::Iterator::Iterator(const Dict *dict, size_t idx)
        : dict(dict), idx(idx)
    {
        skip_dead_entries();
    }

    Dict::EntryView Dict::Iterator::operator*() const
    {
        assert(idx < dict->entries.size());
        const Entry &entry = dict->entries[idx];
        assert(entry.valid());
        return EntryView{entry.key, entry.value};
    }

    Dict::Iterator &Dict::Iterator::operator++()
    {
        assert(idx <= dict->entries.size());
        if(idx < dict->entries.size())
        {
            ++idx;
            skip_dead_entries();
        }
        return *this;
    }

    bool Dict::Iterator::operator==(const Iterator &other) const
    {
        return dict == other.dict && idx == other.idx;
    }

    bool Dict::Iterator::operator!=(const Iterator &other) const
    {
        return !(*this == other);
    }

    void Dict::Iterator::skip_dead_entries()
    {
        while(idx < dict->entries.size() && !dict->entries[idx].valid())
        {
            ++idx;
        }
    }

    Dict::Iterator Dict::begin() const { return Iterator(this, 0); }

    Dict::Iterator Dict::end() const { return Iterator(this, entries.size()); }

    bool Dict::entry_at(size_t idx, EntryView &out) const
    {
        if(idx >= entries.size())
        {
            return false;
        }
        const Entry &entry = entries[idx];
        if(!entry.valid())
        {
            return false;
        }
        out = EntryView{entry.key, entry.value};
        return true;
    }

    Expected<Value> Dict::get_item(ThreadState *thread, Value key)
    {
        ItemResult result = CL_TRY(get_item_if_present(thread, key));
        if(!result.found)
        {
            return Expected<Value>::raise_exception(L"KeyError", L"");
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<Dict::ItemResult> Dict::get_item_if_present(ThreadState *thread,
                                                         Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           can_convert_to<String>(key))
        {
            int32_t idx =
                *find_entry(TValue<String>::from_value_unchecked(key));
            if(idx >= 0)
            {
                return Expected<ItemResult>::ok(
                    ItemResult{entries[idx].value, true});
            }
            return Expected<ItemResult>::ok(ItemResult{Value::None(), false});
        }
        maybe_promote_to_general_shape(thread);
        return general_get_item_if_present(thread, key);
    }

    Expected<Value> Dict::get_item_or_default(ThreadState *thread, Value key,
                                              Value default_value)
    {
        default_value.assert_not_vm_sentinel();
        Owned<Value> live_default(default_value);
        ItemResult result = CL_TRY(get_item_if_present(thread, key));
        return Expected<Value>::ok(result.found ? result.value
                                                : live_default.value());
    }

    Expected<void> Dict::set_item(ThreadState *thread, Value key, Value value)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           can_convert_to<String>(key))
        {
            return set_item_for_str(
                thread, TValue<String>::from_value_unchecked(key), value);
        }
        maybe_promote_to_general_shape(thread);
        return general_set_item(thread, key, value);
    }

    Expected<void> Dict::del_item(ThreadState *thread, Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           can_convert_to<String>(key))
        {
            return del_item_for_str(thread,
                                    TValue<String>::from_value_unchecked(key));
        }
        maybe_promote_to_general_shape(thread);
        return general_del_item(thread, key);
    }

    Expected<bool> Dict::contains(ThreadState *thread, Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           can_convert_to<String>(key))
        {
            return contains_for_str(thread,
                                    TValue<String>::from_value_unchecked(key));
        }
        maybe_promote_to_general_shape(thread);
        return general_contains(thread, key);
    }

    Expected<Value> Dict::pop(ThreadState *thread, Value key)
    {
        ItemResult result = CL_TRY(pop_item_if_present(thread, key));
        if(!result.found)
        {
            return Expected<Value>::raise_exception(L"KeyError", L"");
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<Dict::ItemResult> Dict::pop_item_if_present(ThreadState *thread,
                                                         Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           can_convert_to<String>(key))
        {
            int32_t *entry =
                find_entry(TValue<String>::from_value_unchecked(key));
            int32_t idx = *entry;
            if(idx < 0)
            {
                return Expected<ItemResult>::ok(
                    ItemResult{Value::None(), false});
            }

            Owned<Value> result(entries[idx].value);
            entries.set(idx, Entry(Value::not_present(), Value::None(),
                                   TValue<SMI>::from_smi(0)));
            *entry = tombstone;
            --n_valid_entries;
            return Expected<ItemResult>::ok(ItemResult{result.value(), true});
        }
        maybe_promote_to_general_shape(thread);
        return general_pop_item_if_present(thread, key);
    }

    Expected<Value> Dict::setdefault(ThreadState *thread, Value key,
                                     Value default_value)
    {
        SetDefaultResult result =
            CL_TRY(setdefault_with_presence(thread, key, default_value));
        return Expected<Value>::ok(result.value);
    }

    Expected<Dict::SetDefaultResult>
    Dict::setdefault_with_presence(ThreadState *thread, Value key,
                                   Value default_value)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           can_convert_to<String>(key))
        {
            TValue<String> string_key =
                TValue<String>::from_value_unchecked(key);
            int32_t idx = *find_entry(string_key);
            if(idx >= 0)
            {
                return Expected<SetDefaultResult>::ok(
                    SetDefaultResult{entries[idx].value, true});
            }
            string_keyed_insert(string_key, default_value);
            return Expected<SetDefaultResult>::ok(
                SetDefaultResult{default_value, false});
        }
        maybe_promote_to_general_shape(thread);
        return general_setdefault_with_presence(thread, key, default_value);
    }

    Expected<Value> Dict::get_item_for_str(ThreadState *thread,
                                           TValue<String> key)
    {
        ItemResult result =
            CL_TRY(get_item_if_present(thread, key.raw_value()));
        if(!result.found)
        {
            return Expected<Value>::raise_exception(L"KeyError", L"");
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<void> Dict::set_item_for_str(ThreadState *thread,
                                          TValue<String> key, Value value)
    {
        if(is_exact_dict_string_key_shape(thread, this))
        {
            string_keyed_insert(key, value);
            return Expected<void>::ok();
        }
        return general_set_item(thread, key.raw_value(), value);
    }

    Expected<void> Dict::del_item_for_str(ThreadState *thread,
                                          TValue<String> key)
    {
        if(is_exact_dict_string_key_shape(thread, this))
        {
            Value result = string_keyed_delete(key);
            if(result.is_exception_marker())
            {
                return Expected<void>::propagate_exception();
            }
            return Expected<void>::ok();
        }
        return general_del_item(thread, key.raw_value());
    }

    Expected<bool> Dict::contains_for_str(ThreadState *thread,
                                          TValue<String> key)
    {
        if(is_exact_dict_string_key_shape(thread, this))
        {
            return Expected<bool>::ok(string_keyed_contains(key));
        }
        return general_contains(thread, key.raw_value());
    }

    Expected<Value> Dict::pop_for_str(ThreadState *thread, TValue<String> key)
    {
        ItemResult result =
            CL_TRY(pop_item_if_present(thread, key.raw_value()));
        if(!result.found)
        {
            return Expected<Value>::raise_exception(L"KeyError", L"");
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<Value> Dict::setdefault_for_str(ThreadState *thread,
                                             TValue<String> key,
                                             Value default_value)
    {
        SetDefaultResult result = CL_TRY(
            setdefault_with_presence(thread, key.raw_value(), default_value));
        return Expected<Value>::ok(result.value);
    }

    void Dict::promote_to_general_shape(ThreadState *thread)
    {
        assert(is_exact_dict_string_key_shape(thread, this));
        set_shape(thread->get_exact_dict_general_shape());
        ++table_generation_;
    }

    void Dict::maybe_promote_to_general_shape(ThreadState *thread)
    {
        if(is_exact_dict_string_key_shape(thread, this))
        {
            promote_to_general_shape(thread);
        }
    }

    Expected<size_t>
    Dict::find_entry_slot_for_general_insert(ThreadState *thread, Value key,
                                             TValue<SMI> hash_smi)
    {
        while(true)
        {
            Probe probe = probe_start(hash_smi);

            while(true)
            {
                int32_t entry_status = hash_table[probe.hash_idx];
                if(entry_status == not_present)
                {
                    return Expected<size_t>::ok(probe_write_slot(probe));
                }
                if(entry_status == tombstone)
                {
                    probe_record_tombstone(probe, entry_status);
                    probe_advance(probe);
                    continue;
                }

                Entry entry = entries[entry_status];
                if(entry.hash == hash_smi)
                {
                    if(entry.key == key)
                    {
                        return Expected<size_t>::ok(probe.hash_idx);
                    }

                    Owned<Value> candidate_key(entry.key);
                    bool equal =
                        CL_TRY(thread->test_equal(candidate_key.value(), key));
                    if(!entry_still_matches(probe.table_generation,
                                            probe.hash_idx, entry_status,
                                            candidate_key.value()))
                    {
                        break;
                    }
                    if(!probe_recorded_tombstone_still_available(probe))
                    {
                        probe_clear_recorded_tombstone(probe);
                    }
                    if(equal)
                    {
                        return Expected<size_t>::ok(probe.hash_idx);
                    }
                }

                probe_advance(probe);
            }
        }
    }

    Expected<int32_t>
    Dict::find_entry_index_for_general_lookup(ThreadState *thread, Value key,
                                              TValue<SMI> hash_smi)
    {
        while(true)
        {
            Probe probe = probe_start(hash_smi);

            while(true)
            {
                int32_t entry_status = hash_table[probe.hash_idx];
                if(entry_status == not_present)
                {
                    return Expected<int32_t>::ok(not_present);
                }
                if(entry_status == tombstone)
                {
                    probe_advance(probe);
                    continue;
                }

                Entry entry = entries[entry_status];
                if(entry.hash == hash_smi)
                {
                    if(entry.key == key)
                    {
                        return Expected<int32_t>::ok(entry_status);
                    }

                    Owned<Value> candidate_key(entry.key);
                    bool equal =
                        CL_TRY(thread->test_equal(candidate_key.value(), key));
                    if(!entry_still_matches(probe.table_generation,
                                            probe.hash_idx, entry_status,
                                            candidate_key.value()))
                    {
                        break;
                    }
                    if(equal)
                    {
                        return Expected<int32_t>::ok(entry_status);
                    }
                }

                probe_advance(probe);
            }
        }
    }

    Expected<int64_t>
    Dict::find_entry_slot_for_general_lookup(ThreadState *thread, Value key,
                                             TValue<SMI> hash_smi)
    {
        while(true)
        {
            Probe probe = probe_start(hash_smi);

            while(true)
            {
                int32_t entry_status = hash_table[probe.hash_idx];
                if(entry_status == not_present)
                {
                    return Expected<int64_t>::ok(-1);
                }
                if(entry_status == tombstone)
                {
                    probe_advance(probe);
                    continue;
                }

                Entry entry = entries[entry_status];
                if(entry.hash == hash_smi)
                {
                    if(entry.key == key)
                    {
                        return Expected<int64_t>::ok(
                            static_cast<int64_t>(probe.hash_idx));
                    }

                    Owned<Value> candidate_key(entry.key);
                    bool equal =
                        CL_TRY(thread->test_equal(candidate_key.value(), key));
                    if(!entry_still_matches(probe.table_generation,
                                            probe.hash_idx, entry_status,
                                            candidate_key.value()))
                    {
                        break;
                    }
                    if(equal)
                    {
                        return Expected<int64_t>::ok(
                            static_cast<int64_t>(probe.hash_idx));
                    }
                }

                probe_advance(probe);
            }
        }
    }

    Expected<Dict::ItemResult>
    Dict::general_get_item_if_present(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int32_t idx = CL_TRY(find_entry_index_for_general_lookup(
            thread, live_key.value(), hash));
        if(idx < 0)
        {
            return Expected<ItemResult>::ok(ItemResult{Value::None(), false});
        }
        return Expected<ItemResult>::ok(ItemResult{entries[idx].value, true});
    }

    Expected<void> Dict::set_item_with_known_hash(ThreadState *thread,
                                                  Value key, Value value,
                                                  TValue<SMI> hash)
    {
        key.assert_not_vm_sentinel();
        value.assert_not_vm_sentinel();

        if(is_exact_dict_string_key_shape(thread, this))
        {
            if(can_convert_to<String>(key))
            {
                string_keyed_insert(TValue<String>::from_value_unchecked(key),
                                    value);
                return Expected<void>::ok();
            }
            promote_to_general_shape(thread);
        }

        Owned<Value> live_key(key);
        Owned<Value> live_value(value);
        resize_general_if_needed();

        size_t entry_slot = CL_TRY(
            find_entry_slot_for_general_insert(thread, live_key.value(), hash));
        int32_t idx = hash_table[entry_slot];
        if(idx < 0)
        {
            write_new_at_slot(entry_slot, hash, live_key.value(),
                              live_value.value());
        }
        else
        {
            write_existing(idx, live_value.value());
        }

        return Expected<void>::ok();
    }

    Expected<void> Dict::general_set_item(ThreadState *thread, Value key,
                                          Value value)
    {
        key.assert_not_vm_sentinel();
        value.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        Owned<Value> live_value(value);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));

        resize_general_if_needed();

        size_t entry_slot = CL_TRY(
            find_entry_slot_for_general_insert(thread, live_key.value(), hash));
        int32_t idx = hash_table[entry_slot];
        if(idx < 0)
        {
            write_new_at_slot(entry_slot, hash, live_key.value(),
                              live_value.value());
        }
        else
        {
            write_existing(idx, live_value.value());
        }

        return Expected<void>::ok();
    }

    Expected<void> Dict::general_del_item(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int64_t entry_slot = CL_TRY(
            find_entry_slot_for_general_lookup(thread, live_key.value(), hash));
        if(entry_slot < 0)
        {
            return Expected<void>::raise_exception(L"KeyError", L"");
        }

        int32_t idx = hash_table[static_cast<size_t>(entry_slot)];
        assert(idx >= 0);
        (void)idx;
        delete_entry_at_slot(static_cast<size_t>(entry_slot));
        return Expected<void>::ok();
    }

    Expected<bool> Dict::general_contains(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int32_t idx = CL_TRY(find_entry_index_for_general_lookup(
            thread, live_key.value(), hash));
        return Expected<bool>::ok(idx >= 0);
    }

    Expected<Dict::ItemResult>
    Dict::general_pop_item_if_present(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int64_t entry_slot = CL_TRY(
            find_entry_slot_for_general_lookup(thread, live_key.value(), hash));
        if(entry_slot < 0)
        {
            return Expected<ItemResult>::ok(ItemResult{Value::None(), false});
        }

        int32_t idx = hash_table[static_cast<size_t>(entry_slot)];
        assert(idx >= 0);
        Owned<Value> result(entries[idx].value);
        delete_entry_at_slot(static_cast<size_t>(entry_slot));
        return Expected<ItemResult>::ok(ItemResult{result.value(), true});
    }

    Expected<Dict::SetDefaultResult>
    Dict::general_setdefault_with_presence(ThreadState *thread, Value key,
                                           Value default_value)
    {
        key.assert_not_vm_sentinel();
        default_value.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        Owned<Value> live_default(default_value);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));

        resize_general_if_needed();

        size_t entry_slot = CL_TRY(
            find_entry_slot_for_general_insert(thread, live_key.value(), hash));
        int32_t slot_value = hash_table[entry_slot];
        if(slot_value >= 0)
        {
            return Expected<SetDefaultResult>::ok(
                SetDefaultResult{entries[slot_value].value, true});
        }

        write_new_at_slot(entry_slot, hash, live_key.value(),
                          live_default.value());
        return Expected<SetDefaultResult>::ok(
            SetDefaultResult{live_default.value(), false});
    }

    const int32_t *Dict::find_entry(TValue<String> key) const
    {
        return find_entry_with_provided_hash(key, internal_hash(key));
    }

    int32_t *Dict::find_entry(TValue<String> key)
    {
        return find_entry_with_provided_hash(key, internal_hash(key));
    }

    int32_t *Dict::find_entry_with_provided_hash(TValue<String> key,
                                                 TValue<SMI> hash_smi)
    {
        const Dict *self = this;
        return const_cast<int32_t *>(
            self->find_entry_with_provided_hash(key, hash_smi));
    }

    const int32_t *
    Dict::find_entry_with_provided_hash(TValue<String> key,
                                        TValue<SMI> hash_smi) const
    {
        uint64_t hash = hash_smi.extract();
        uint32_t hash_table_size_m1 = hash_table.size() - 1;

        uint32_t hash_idx = hash & hash_table_size_m1;
        int32_t tombstone_hash_idx = -1;
        while(true)
        {
            int32_t entry_idx = hash_table[hash_idx];
            if(entry_idx == not_present)
            {
                if(tombstone_hash_idx == -1)
                    tombstone_hash_idx = hash_idx;
                return &hash_table[tombstone_hash_idx];
            }
            if(entry_idx == tombstone)
            {
                if(tombstone_hash_idx == -1)
                {
                    tombstone_hash_idx = hash_idx;
                }
                hash_idx = (hash_idx + 1) & hash_table_size_m1;
                continue;
            }
            if(internal_eq(key, TValue<String>::from_value_unchecked(
                                    entries[entry_idx].key)))
            {
                return &hash_table[hash_idx];
            }

            hash_idx = (hash_idx + 1) & hash_table_size_m1;
        }
    }

    Value Dict::string_keyed_lookup(TValue<String> key) const
    {
        const int32_t *iidx = find_entry(key);
        int32_t idx = *iidx;
        if(idx >= 0)
        {
            const Entry &e = entries[idx];
            if(e.valid())
            {
                return e.value;
            }
        }
        return active_thread()->set_pending_builtin_exception_none(L"KeyError");
    }

    Value Dict::string_keyed_delete(TValue<String> key)
    {
        int32_t *iidx = find_entry(key);
        int32_t idx = *iidx;
        if(idx >= 0)
        {
            entries.set(idx, Entry(Value::not_present(), Value::None(),
                                   TValue<SMI>::from_smi(0)));
            *iidx = tombstone;
            --n_valid_entries;
            return Value::None();
        }
        return active_thread()->set_pending_builtin_exception_none(L"KeyError");
    }

    void Dict::string_keyed_insert(TValue<String> key, Value value)
    {
        value.assert_not_vm_sentinel();

        if(entries.size() > hash_table.size() * max_load_nom / max_load_denom)
        {
            grow();
        }

        TValue<SMI> hash = internal_hash(key);
        int32_t *entry = find_entry_with_provided_hash(key, hash);
        int32_t idx = *entry;
        if(idx < 0)
        {
            idx = entries.size();
            *entry = idx;
            entries.emplace_back(key.raw_value(), value, hash);
            ++n_valid_entries;
        }
        else
        {
            Entry existing = entries[idx];
            entries.set(idx, Entry(existing.key, value, existing.hash));
        }
    }

    bool Dict::string_keyed_contains(TValue<String> key) const
    {
        return *find_entry(key) >= 0;
    }

    void Dict::clear()
    {
        entries.clear();
        n_valid_entries = 0;
        ++table_generation_;
        for(int32_t &k: hash_table)
        {
            k = not_present;
        }
    }

    void Dict::grow()
    {
        // make one that's twice the size
        size_t new_size = hash_table.size() * 2;
        ++table_generation_;
        hash_table.resize(0);
        hash_table.resize(new_size, -1);

        size_t write_idx = 0;
        for(size_t read_idx = 0; read_idx < entries.size(); ++read_idx)
        {
            Entry entry = entries[read_idx];
            if(!entry.valid())
            {
                continue;
            }

            if(write_idx != read_idx)
            {
                entries.set(write_idx, entry);
            }
            uint64_t hash = entries[write_idx].hash.extract();
            uint32_t hash_table_size_m1 = hash_table.size() - 1;
            uint32_t hash_idx = hash & hash_table_size_m1;
            while(hash_table[hash_idx] != not_present)
            {
                hash_idx = (hash_idx + 1) & hash_table_size_m1;
            }
            hash_table[hash_idx] = static_cast<int32_t>(write_idx);
            ++write_idx;
        }
        entries.resize(write_idx, Entry(Value::not_present(), Value::None(),
                                        TValue<SMI>::from_smi(0)));
        assert(entries.size() == n_valid_entries);
    }

}  // namespace cl
