#include "dict.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "native_function.h"
#include "refcount.h"
#include "str.h"
#include "string_builder.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{

    /*
      TODO: these just assume string keys. replace with full equality machinery
      when we have calling Python-defined methods from C++ up and running */

    static TValue2<SMI> internal_hash(Value key)
    {
        return TValue2<SMI>::from_smi(
            string_hash(TValue2<String>::from_value_unchecked(key)));
    }

    static bool internal_eq(Value a, Value b)
    {
        return string_eq(TValue2<String>::from_value_unchecked(a),
                         TValue2<String>::from_value_unchecked(b));
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
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"dict"),
            Dict::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }

    static Value native_dict_repr(Value self)
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

    static Value native_dict_len(Value self)
    {
        if(!can_convert_to<Dict>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict.__len__ expects a dict receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<Dict>()->size()));
    }

    void install_dict_class_methods(VirtualMachine *vm)
    {
        BuiltinNativeMethod methods[] = {
            builtin_native_method(L"__str__", native_dict_repr,
                                  L"Return str(self)."),
            builtin_native_method(L"__repr__", native_dict_repr,
                                  L"Return repr(self)."),
            builtin_native_method(L"__len__", native_dict_len,
                                  L"Return len(self)."),
        };
        install_builtin_native_methods(vm, vm->dict_class(), methods,
                                       std::size(methods));
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

    const int32_t *Dict::find_entry(Value key) const
    {
        return find_entry_with_provided_hash(key, internal_hash(key));
    }

    int32_t *Dict::find_entry(Value key)
    {
        return find_entry_with_provided_hash(key, internal_hash(key));
    }

    int32_t *Dict::find_entry_with_provided_hash(Value key,
                                                 TValue2<SMI> hash_smi)
    {
        const Dict *self = this;
        return const_cast<int32_t *>(
            self->find_entry_with_provided_hash(key, hash_smi));
    }

    const int32_t *
    Dict::find_entry_with_provided_hash(Value key, TValue2<SMI> hash_smi) const
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
                                   TValue2<SMI>::from_smi(0)));
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

        TValue2<SMI> hash = internal_hash(key);
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
                                        TValue2<SMI>::from_smi(0)));
        assert(entries.size() == n_valid_entries);
    }

}  // namespace cl
