#include "scope.h"
#include "str.h"
#include <stdexcept>

namespace cl
{

    Scope::Scope(Value _parent_scope)
        : Object(&klass, compact_layout()), parent_scope(_parent_scope),
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
            if(string_eq(
                   key, TValue<String>::unsafe_unchecked(slot_names[slot_idx])))
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
        RawArray<int32_t> new_name_table(name_table.size() * 2,
                                         hash_not_present);
        std::swap(name_table, new_name_table);

        for(int32_t entry_idx = 0; entry_idx < int32_t(entries.size());
            ++entry_idx)
        {
            int32_t slot_idx = entries[entry_idx].get_slot_idx();
            if(slot_idx < 0)
            {
                continue;
            }

            int32_t *name_table_entry = find_name_table_entry(
                TValue<String>::unsafe_unchecked(slot_names[slot_idx]));
            *name_table_entry = slot_idx;
        }
    }

    int32_t Scope::append_entry(int32_t slot_idx)
    {
        int32_t entry_idx = entries.size();
        entries.emplace_back(slot_idx);
        slot_current_entry_indices[slot_idx] = entry_idx;
        return entry_idx;
    }

    int32_t Scope::allocate_slot(TValue<String> key, Value initial_value)
    {
        int32_t slot_idx = slot_values.size();
        slot_values.emplace_back(initial_value);
        slot_names.emplace_back(key.as_value());
        slot_current_entry_indices.emplace_back(-1);
        return slot_idx;
    }

    void Scope::revive_slot(int32_t slot_idx)
    {
        int32_t current_entry_idx = slot_current_entry_indices[slot_idx];
        if(current_entry_idx >= 0)
        {
            entries[current_entry_idx].set_slot_idx(-1);
        }

        int32_t new_entry_idx = append_entry(slot_idx);
        (void)new_entry_idx;

        TValue<String> name =
            TValue<String>::unsafe_unchecked(slot_names[slot_idx]);
        int32_t *name_table_entry = find_name_table_entry(name);
        *name_table_entry = slot_idx;
    }

    TValue<String> Scope::get_name_by_slot_index(int32_t slot_idx) const
    {
        assert(slot_names[slot_idx] != Value::None());
        return TValue<String>::unsafe_unchecked(slot_names[slot_idx]);
    }

    /* For a write, we just insert a regular not-present value with no parent
     * scope slot indication (-1) */
    int32_t Scope::register_slot_index_for_write(TValue<String> key)
    {
        int32_t slot_idx = lookup_slot_index_local(key);
        if(slot_idx >= 0)
        {
            return slot_idx;
        }

        slot_idx = allocate_slot(key, Value::not_present(-1));
        maybe_grow_name_table();
        int32_t *name_table_entry = find_name_table_entry(key);
        *name_table_entry = slot_idx;
        return slot_idx;
    }

    /* whereas for a read, if the value isn't present, we also
     * register the slot in the parent scope and save that slot in
     * the not-present value. This is to accelerate access to
     * builtins, which live in the parent scope
     */
    int32_t Scope::register_slot_index_for_read(TValue<String> key)
    {
        int32_t slot_idx = lookup_slot_index_local(key);
        if(slot_idx >= 0)
        {
            return slot_idx;
        }

        int32_t parent_idx = -1;
        if(parent_scope != Value::None())
        {
            parent_idx =
                get_parent_scope_ptr()->register_slot_index_for_read(key);
        }

        slot_idx = allocate_slot(key, Value::not_present(parent_idx));
        maybe_grow_name_table();
        int32_t *name_table_entry = find_name_table_entry(key);
        *name_table_entry = slot_idx;
        return slot_idx;
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

    Value Scope::get_by_name(TValue<String> name) const
    {
        int32_t slot_idx = lookup_slot_index_local(name);
        if(slot_idx >= 0)
        {
            return get_by_slot_index(slot_idx);
        }
        if(parent_scope != Value::None())
        {
            return get_parent_scope_ptr()->get_by_name(name);
        }
        return Value::not_present();
    }

    void Scope::set_by_name(TValue<String> name, Value val)
    {
        int32_t slot_idx = lookup_slot_index_local(name);
        if(slot_idx < 0)
        {
            slot_idx = allocate_slot(name, Value::not_present(-1));
            maybe_grow_name_table();
            int32_t *name_table_entry = find_name_table_entry(name);
            *name_table_entry = slot_idx;
        }
        set_by_slot_index(slot_idx, val);
    }

    void Scope::reserve_empty_slots(size_t n_slots)
    {
        for(size_t i = 0; i < n_slots; ++i)
        {
            slot_values.emplace_back(Value::not_present());
            slot_names.emplace_back(Value::None());
            slot_current_entry_indices.emplace_back(-1);
        }
    }

};  // namespace cl
