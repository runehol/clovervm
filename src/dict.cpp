#include "dict.h"
#include "class_object.h"
#include "refcount.h"
#include "str.h"
#include "virtual_machine.h"

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
        : Object(cls, native_layout_id, compact_layout()),
          hash_table(min_table_size, not_present), n_valid_entries(0)
    {
    }

    Dict::Dict(ClassObject *cls, const Dict &other)
        : Object(cls, native_layout_id, compact_layout()),
          hash_table(min_table_size, not_present), n_valid_entries(0)
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
            vm->get_or_create_interned_string_value(L"dict"), 1, nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
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
        throw std::runtime_error("KeyError");
    }

    void Dict::del_item(Value key)
    {
        int32_t *iidx = find_entry(key);
        int32_t idx = *iidx;
        if(idx >= 0)
        {
            entries.set(idx, Entry(Value::not_present(), Value::None(),
                                   TValue<SMI>::from_smi(0)));
            *iidx = tombstone;
            --n_valid_entries;
        }
        else
        {
            throw std::runtime_error("KeyError");
        }
    }

    void Dict::set_item(Value key, Value value)
    {
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

        // and then just insert all the keys again
        for(size_t idx = 0; idx < entries.size(); ++idx)
        {
            if(entries[idx].valid())
            {
                int32_t *entry = find_entry_with_provided_hash(
                    entries[idx].key, entries[idx].hash);
                *entry = idx;
            }
        }
    }

}  // namespace cl
