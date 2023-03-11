#include "indirect_dict.h"
#include "str.h"
#include "refcount.h"

namespace cl
{
    IndirectDict::IndirectDict()
        : Object(&klass, 0, 8),
          hash_table(16, -1)

    {}

    const int32_t *IndirectDict::find_entry(Value key) const
    {
        uint64_t hash = string_hash(key);
        uint32_t hash_table_size_m1 = hash_table.size() - 1;

        uint32_t hash_idx = hash & hash_table_size_m1;
        int32_t tombstone_hash_idx = -1;
        while(true)
        {
            int32_t entry_idx = hash_table[hash_idx];
            if(entry_idx == not_present)
            {
                if(tombstone_hash_idx == -1) tombstone_hash_idx = hash_idx;
                return &hash_table[tombstone_hash_idx];
            }
            if(entry_idx == tombstone)
            {
                if(tombstone_hash_idx == -1)
                {
                    tombstone_hash_idx = hash_idx;
                }
            }
            if(string_eq(key, keys[entry_idx]))
            {
                return &hash_table[hash_idx];
            }

            hash_idx = (hash_idx+1) & hash_table_size_m1;

        }
    }

    int32_t *IndirectDict::find_entry(Value key)
    {
        const IndirectDict *self = this;
        return const_cast<int32_t *>(self->find_entry(key));
    }

    int32_t IndirectDict::insert(Value key)
    {
        if(keys.size() > hash_table.size() * max_load_nom / max_load_denom)
        {
            grow();
        }

        int32_t *entry = find_entry(key);
        int32_t idx = *entry;
        if(idx < 0)
        {
            idx = keys.size();
            *entry = idx;
            keys.push_back(incref(key));
        }
        return idx;
    }

    void IndirectDict::reserve_empty_slots(size_t n_slots)
    {
        for(size_t i = 0; i < n_slots; ++i)
        {
            if(keys.size() > hash_table.size() * max_load_nom / max_load_denom)
            {
                grow();
            }
            keys.push_back(Value::not_present());
        }
    }

    int32_t IndirectDict::lookup(Value key) const
    {
        const int32_t *entry = find_entry(key);
        int32_t idx = *entry;
        return std::max(idx, -1); // normalise tombstone to not-found
    }

    void IndirectDict::grow()
    {
        // make one that's twice the size
        std::vector<int32_t> new_hash_table(hash_table.size()*2, -1);
        std::swap(hash_table, new_hash_table);

        //and then just insert all the keys again
        for(int32_t idx = 0; idx < int32_t(keys.size()); ++idx)
        {
            if(!keys[idx].is_not_present())
            {
                int32_t *entry = find_entry(keys[idx]);
                *entry = idx;
            }
        }
    }


}
