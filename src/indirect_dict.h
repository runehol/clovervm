#ifndef CL_INDIRECT_DICT_H
#define CL_INDIRECT_DICT_H

#include <vector>
#include "object.h"
#include "klass.h"

namespace cl
{


    class IndirectDict : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"indirect_dict", nullptr);

        IndirectDict();

        int32_t insert(Value key);
        int32_t lookup(Value key) const;

        uint32_t size() const { return keys.size(); }
        bool empty() const { return keys.empty(); }

        void reserve_empty_slots(size_t n_slots);


        Value get_key_by_slot_index(int32_t slot_idx) const
        {
            return keys[slot_idx];
        }

    private:
        constexpr static uint32_t max_load_nom = 3;
        constexpr static uint32_t max_load_denom = 4;
        constexpr static int32_t tombstone = -2;
        constexpr static int32_t not_present = -1;

        const int32_t *find_entry(Value key) const;
        int32_t *find_entry(Value key);

        void grow();

        // TODO these need to be CL arrays at some point, but we're not ready for that yet
        std::vector<int32_t> hash_table;
        std::vector<Value> keys;


    };

};



#endif //CL_INDIRECT_DICT_H
