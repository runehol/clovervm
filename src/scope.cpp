#include "scope.h"
#include "str.h"
#include <stdexcept>

namespace cl
{
    Scope::Scope(Scope *_parent_scope)
        : HeapObject(native_layout), parent_scope(_parent_scope),
          name_table(16, hash_not_present)
    {
    }

    const int32_t *Scope::find_name_table_entry(TValue<String> key) const
    {
        uint64_t hash = string_hash(key);
        uint32_t table_size_m1 = name_table.size() - 1;

        uint32_t hash_idx = hash & table_size_m1;
        while(true)
        {
            int32_t slot_idx = name_table[hash_idx];
            if(slot_idx == hash_not_present)
            {
                return &name_table[hash_idx];
            }
            if(string_eq(key, TValue<String>::from_value_unchecked(
                                  slot_names[slot_idx])))
            {
                return &name_table[hash_idx];
            }

            hash_idx = (hash_idx + 1) & table_size_m1;
        }
    }

    int32_t *Scope::find_name_table_entry(TValue<String> key)
    {
        const Scope *self = this;
        return const_cast<int32_t *>(self->find_name_table_entry(key));
    }

    void Scope::grow_name_table()
    {
        size_t new_size = name_table.size() * 2;
        name_table.resize(0);
        name_table.resize(new_size, hash_not_present);

        for(int32_t entry_idx = 0; entry_idx < int32_t(entries.size());
            ++entry_idx)
        {
            int32_t slot_idx = entries[entry_idx];
            int32_t *name_table_entry = find_name_table_entry(
                TValue<String>::from_value_unchecked(slot_names[slot_idx]));
            *name_table_entry = slot_idx;
        }
    }

    int32_t Scope::append_entry(int32_t slot_idx)
    {
        int32_t entry_idx = entries.size();
        entries.emplace_back(slot_idx);
        return entry_idx;
    }

    int32_t Scope::allocate_named_slot(TValue<String> key)
    {
        int32_t slot_idx = slot_names.size();
        slot_names.emplace_back(key.raw_value());
        append_entry(slot_idx);
        return slot_idx;
    }

    TValue<String> Scope::get_name_by_slot_index(int32_t slot_idx) const
    {
        assert(slot_names[slot_idx] != Value::None());
        return TValue<String>::from_value_unchecked(slot_names[slot_idx]);
    }

    int32_t Scope::register_slot_index_for_write(TValue<String> key)
    {
        int32_t slot_idx = lookup_slot_index_local(key);
        if(slot_idx >= 0)
        {
            return slot_idx;
        }

        slot_idx = allocate_named_slot(key);
        maybe_grow_name_table();
        int32_t *name_table_entry = find_name_table_entry(key);
        *name_table_entry = slot_idx;
        return slot_idx;
    }

    int32_t Scope::register_slot_index_for_read(TValue<String> key)
    {
        return register_slot_index_for_write(key);
    }

    int32_t Scope::lookup_slot_index_local(TValue<String> name) const
    {
        const int32_t *name_table_entry = find_name_table_entry(name);
        int32_t slot_idx = *name_table_entry;
        if(slot_idx < 0)
        {
            return -1;
        }
        return slot_idx;
    }

    void Scope::reserve_empty_slots(size_t n_slots)
    {
        for(size_t i = 0; i < n_slots; ++i)
        {
            slot_names.emplace_back(Value::None());
        }
    }

};  // namespace cl
