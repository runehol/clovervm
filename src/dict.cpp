#include "dict.h"
#include "class_object.h"
#include "dict_view.h"
#include "exception_propagation.h"
#include "list.h"
#include "native_function.h"
#include "owned.h"
#include "refcount.h"
#include "str.h"
#include "string_builder.h"
#include "thread_state.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{

    /*
      TODO: these just assume string keys. replace with full equality machinery
      when we have calling Python-defined methods from C++ up and running */

    static TValue<SMI> internal_hash(Value key)
    {
        return TValue<SMI>::from_smi(
            string_hash(TValue<String>::from_value_unchecked(key)));
    }

    static bool internal_eq(Value a, Value b)
    {
        return string_eq(TValue<String>::from_value_unchecked(a),
                         TValue<String>::from_value_unchecked(b));
    }

    Dict::Dict(ClassObject *cls)
        : Object(cls, native_layout), hash_table(min_table_size, not_present),
          n_valid_entries(0)
    {
    }

    Dict::Dict(ClassObject *cls, const Dict &other)
        : Object(cls, native_layout), hash_table(min_table_size, not_present),
          n_valid_entries(0)
    {
        for(const Entry &e: other.entries)
        {
            if(e.valid())
            {
                set_item(e.key, e.value);
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
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
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

    static Value require_string_key(Value key)
    {
        if(!can_convert_to<String>(key))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict keys must be str");
        }
        return Value::None();
    }

    static Value require_tuple_string_keys(const Tuple *keys)
    {
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            CL_PROPAGATE_EXCEPTION(
                require_string_key(keys->item_unchecked(idx)));
        }
        return Value::None();
    }

    static Value require_list_string_keys(const List *keys)
    {
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            CL_PROPAGATE_EXCEPTION(
                require_string_key(keys->item_unchecked(idx)));
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
        CL_PROPAGATE_EXCEPTION(require_string_key(key));
        Dict *dict = self.get_ptr<Dict>();
        if(!dict->contains(key))
        {
            return default_value;
        }
        return dict->get_item(key);
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
        CL_PROPAGATE_EXCEPTION(require_string_key(key));
        return self.get_ptr<Dict>()->pop(key);
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
        CL_PROPAGATE_EXCEPTION(require_string_key(key));
        return self.get_ptr<Dict>()->setdefault(key, default_value);
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

        self.get_ptr<Dict>()->update_from_dict(other.get_ptr<Dict>());
        return Value::None();
    }

    static Value native_dict_fromkeys(ThreadState *thread, Value keys,
                                      Value value)
    {
        if(can_convert_to<Tuple>(keys))
        {
            Tuple *tuple = keys.get_ptr<Tuple>();
            CL_PROPAGATE_EXCEPTION(require_tuple_string_keys(tuple));
            return Dict::from_tuple_keys(tuple, value);
        }
        if(can_convert_to<List>(keys))
        {
            List *list = keys.get_ptr<List>();
            CL_PROPAGATE_EXCEPTION(require_list_string_keys(list));
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

    Value Dict::pop(Value key)
    {
        if(!contains(key))
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"KeyError");
        }
        Value result = get_item(key);
        CL_PROPAGATE_EXCEPTION(del_item(key));
        return result;
    }

    Value Dict::popitem()
    {
        if(empty())
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"KeyError");
        }

        EntryView last = {Value::not_present(), Value::not_present()};
        for(EntryView entry: *this)
        {
            last = entry;
        }
        assert(!last.key.is_not_present());
        CL_PROPAGATE_EXCEPTION(del_item(last.key));
        Owned<TValue<Tuple>> result(make_object_value<Tuple>(2));
        result.extract()->initialize_item_unchecked(0, last.key);
        result.extract()->initialize_item_unchecked(1, last.value);
        return result.raw_value();
    }

    Value Dict::setdefault(Value key, Value default_value)
    {
        if(contains(key))
        {
            return get_item(key);
        }
        set_item(key, default_value);
        return default_value;
    }

    void Dict::update_from_dict(const Dict *other)
    {
        for(EntryView entry: *other)
        {
            set_item(entry.key, entry.value);
        }
    }

    Value Dict::from_tuple_keys(const Tuple *keys, Value value)
    {
        Owned<TValue<Dict>> result(make_object_value<Dict>());
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            result.extract()->set_item(keys->item_unchecked(idx), value);
        }
        return result.raw_value();
    }

    Value Dict::from_list_keys(const List *keys, Value value)
    {
        Owned<TValue<Dict>> result(make_object_value<Dict>());
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            result.extract()->set_item(keys->item_unchecked(idx), value);
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

    const int32_t *Dict::find_entry(Value key) const
    {
        return find_entry_with_provided_hash(key, internal_hash(key));
    }

    int32_t *Dict::find_entry(Value key)
    {
        return find_entry_with_provided_hash(key, internal_hash(key));
    }

    int32_t *Dict::find_entry_with_provided_hash(Value key,
                                                 TValue<SMI> hash_smi)
    {
        const Dict *self = this;
        return const_cast<int32_t *>(
            self->find_entry_with_provided_hash(key, hash_smi));
    }

    const int32_t *
    Dict::find_entry_with_provided_hash(Value key, TValue<SMI> hash_smi) const
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
            if(internal_eq(key, entries[entry_idx].key))
            {
                return &hash_table[hash_idx];
            }

            hash_idx = (hash_idx + 1) & hash_table_size_m1;
        }
    }

    Value Dict::get_item(Value key) const
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

    Value Dict::del_item(Value key)
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

    void Dict::set_item(Value key, Value value)
    {
        key.assert_not_vm_sentinel();
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
            entries.emplace_back(key, value, hash);
            ++n_valid_entries;
        }
        else
        {
            Entry existing = entries[idx];
            entries.set(idx, Entry(existing.key, value, existing.hash));
        }
    }

    bool Dict::contains(Value key) const { return *find_entry(key) >= 0; }

    void Dict::clear()
    {
        entries.clear();
        n_valid_entries = 0;
        for(int32_t &k: hash_table)
        {
            k = not_present;
        }
    }

    void Dict::grow()
    {
        // make one that's twice the size
        size_t new_size = hash_table.size() * 2;
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
            int32_t *hash_entry = find_entry_with_provided_hash(
                entries[write_idx].key, entries[write_idx].hash);
            *hash_entry = static_cast<int32_t>(write_idx);
            ++write_idx;
        }
        entries.resize(write_idx, Entry(Value::not_present(), Value::None(),
                                        TValue<SMI>::from_smi(0)));
        assert(entries.size() == n_valid_entries);
    }

}  // namespace cl
