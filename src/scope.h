#ifndef CL_SCOPE_H
#define CL_SCOPE_H

#include "native_layout_declarations.h"
#include "object.h"
#include "owned.h"
#include "str.h"
#include "typed_value.h"
#include "value.h"
#include "vm_array.h"
#include <cstdint>

namespace cl
{
    class Scope : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Scope;

        Scope(Scope *_parent_scope);

        int32_t register_slot_index_for_write(TValue<String> key);

        int32_t register_slot_index_for_read(TValue<String> key);

        int32_t lookup_slot_index_local(TValue<String> name) const;

        void reserve_empty_slots(size_t n_slots);

        uint32_t size() const { return slot_names.size(); }
        bool empty() const { return slot_names.empty(); }
        bool slot_is_named(int32_t slot_idx) const
        {
            return slot_names[slot_idx] != Value::None();
        }
        TValue<String> get_name_by_slot_index(int32_t slot_idx) const;
        uint32_t entry_count() const { return entries.size(); }
        int32_t get_entry_slot_index(uint32_t entry_idx) const
        {
            return entries[entry_idx];
        }
        TValue<String> get_entry_key(uint32_t entry_idx) const
        {
            int32_t slot_idx = entries[entry_idx];
            return TValue<String>::from_value_unchecked(slot_names[slot_idx]);
        }

    private:
        static constexpr uint32_t max_load_nom = 3;
        static constexpr uint32_t max_load_denom = 4;
        static constexpr int32_t hash_not_present = -1;

        const int32_t *find_name_table_entry(TValue<String> key) const;
        int32_t *find_name_table_entry(TValue<String> key);
        void maybe_grow_name_table()
        {
            if(slot_names.size() >
               name_table.size() * max_load_nom / max_load_denom)
            {
                grow_name_table();
            }
        }
        void grow_name_table();
        int32_t append_entry(int32_t slot_idx);
        int32_t allocate_named_slot(TValue<String> key);

        MemberHeapPtr<Scope> parent_scope;
        RawArray<int32_t> name_table;
        RawArray<int32_t> entries;
        ValueArray<Value> slot_names;

    public:
        CL_DECLARE_STATIC_VALUE_SPAN(
            Scope, parent_scope,
            1 + decltype(name_table)::embedded_value_count +
                decltype(entries)::embedded_value_count +
                decltype(slot_names)::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(Scope);
    };

}  // namespace cl

#endif  // CL_SCOPE_H
