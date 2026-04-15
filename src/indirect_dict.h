#ifndef CL_INDIRECT_DICT_H
#define CL_INDIRECT_DICT_H

#include "klass.h"
#include "object.h"
#include "owned.h"
#include "typed_value.h"
#include <vector>

namespace cl
{

    class IndirectDict : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"indirect_dict", nullptr);

        IndirectDict();

        int32_t insert(TValue<String> key);
        int32_t lookup(TValue<String> key) const;

        uint32_t size() const { return keys.size(); }
        bool empty() const { return keys.empty(); }

        void reserve_empty_slots(size_t n_slots);

        TValue<String> get_key_by_slot_index(int32_t slot_idx) const
        {
            Value key = keys[slot_idx];
            assert(!key.is_not_present());
            return TValue<String>::unsafe_unchecked(key);
        }

    private:
        constexpr static uint32_t max_load_nom = 3;
        constexpr static uint32_t max_load_denom = 4;
        constexpr static int32_t tombstone = -2;
        constexpr static int32_t not_present = -1;

        const int32_t *find_entry(TValue<String> key) const;
        int32_t *find_entry(TValue<String> key);

        void grow();

        // TODO these need to be CL arrays at some point, but we're not ready
        // for that yet
        std::vector<int32_t> hash_table;
        std::vector<OwnedValue> keys;

    public:
        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(IndirectDict);
    };

};  // namespace cl

#endif  // CL_INDIRECT_DICT_H
