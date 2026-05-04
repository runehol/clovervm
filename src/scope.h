#ifndef CL_SCOPE_H
#define CL_SCOPE_H

#include "object.h"
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
        Scope(Scope *_parent_scope);

        /* For a write, we just insert a regular not-present value with no
         * parent scope slot indication (-1) */
        int32_t register_slot_index_for_write(TValue<String> key);

        /* whereas for a read, if the value isn't present, we also
         * register the slot in the parent scope and save that slot in
         * the not-present value. This is to accelerate access to
         * builtins, which live in the parent scope
         */
        int32_t register_slot_index_for_read(TValue<String> key);

        int32_t lookup_slot_index_local(TValue<String> name) const;

        Value get_by_name(TValue<String> name) const;

        ALWAYSINLINE bool slot_is_live(int32_t slot_idx) const
        {
            return !slot_values[slot_idx].is_not_present();
        }

        ALWAYSINLINE bool entry_is_live(int32_t entry_idx) const
        {
            return entries[entry_idx].get_slot_idx() >= 0;
        }

        ALWAYSINLINE Value
        get_by_slot_index_fastpath_only(int32_t slot_idx) const
        {
            assert(slot_idx >= 0);
            Value v = slot_values[slot_idx];
            if(slot_is_live(slot_idx))
                return v;
            int32_t parent_slot_idx = v.get_not_present_index();
            if(likely(parent_slot_idx >= 0))
            {
                MUSTTAIL return get_parent_scope_ptr()
                    ->get_by_slot_index_fastpath_only(parent_slot_idx);
            }
            else
            {
                return Value::not_present();
            }
        }

        NOINLINE Value get_by_slot_index(int32_t slot_idx) const
        {
            assert(slot_idx >= 0);
            Value v = slot_values[slot_idx];
            if(slot_is_live(slot_idx))
                return v;
            int32_t parent_slot_idx = v.get_not_present_index();
            if(likely(parent_slot_idx >= 0))
            {
                return get_parent_scope_ptr()->get_by_slot_index(
                    parent_slot_idx);
            }
            else
            {
                if(unlikely(parent_scope != nullptr))
                {
                    return get_parent_scope_ptr()->get_by_name(
                        get_name_by_slot_index(slot_idx));
                }
                return Value::not_present();
            }
        }

        void set_by_name(TValue<String> name, Value val);

        ALWAYSINLINE void set_by_slot_index(int32_t slot_idx, Value val)
        {
            val.assert_not_exception_marker();
            if(unlikely(!slot_is_live(slot_idx) && !val.is_not_present() &&
                        slot_names[slot_idx] != Value::None()))
            {
                revive_slot(slot_idx);
            }
            slot_values.set(slot_idx, val);
        }

        ALWAYSINLINE bool set_by_slot_index_needs_slow_path(int32_t slot_idx,
                                                            Value val) const
        {
            assert(slot_idx >= 0);
            val.assert_not_exception_marker();
            return !slot_is_live(slot_idx) && !val.is_not_present() &&
                   slot_names[slot_idx] != Value::None();
        }

        ALWAYSINLINE HeapObject *swap_by_slot_index(int32_t slot_idx, Value val)
        {
            val.assert_not_vm_sentinel();
            assert(!set_by_slot_index_needs_slow_path(slot_idx, val));
            return slot_values.swap_slot(slot_idx, val);
        }

        void reserve_empty_slots(size_t n_slots);

        uint32_t size() const { return slot_values.size(); }
        bool empty() const { return slot_values.empty(); }
        bool slot_is_named(int32_t slot_idx) const
        {
            return slot_names[slot_idx] != Value::None();
        }
        TValue<String> get_name_by_slot_index(int32_t slot_idx) const;
        uint32_t entry_count() const { return entries.size(); }
        int32_t get_entry_slot_index(uint32_t entry_idx) const
        {
            return entries[entry_idx].get_slot_idx();
        }
        TValue<String> get_entry_key(uint32_t entry_idx) const
        {
            int32_t slot_idx = entries[entry_idx].get_slot_idx();
            assert(slot_idx >= 0);
            return TValue<String>::from_value_unchecked(slot_names[slot_idx]);
        }

    private:
        class Entry
        {
        public:
            explicit Entry(int32_t _slot_idx) : slot_idx(_slot_idx) {}

            int32_t get_slot_idx() const { return slot_idx; }
            void set_slot_idx(int32_t idx) { slot_idx = idx; }

        private:
            int32_t slot_idx;
        };

        static constexpr uint32_t max_load_nom = 3;
        static constexpr uint32_t max_load_denom = 4;
        static constexpr int32_t hash_not_present = -1;

        Scope *get_parent_scope_ptr() const { return parent_scope.extract(); }
        const int32_t *find_name_table_entry(TValue<String> key) const;
        int32_t *find_name_table_entry(TValue<String> key);
        void maybe_grow_name_table()
        {
            if(slot_values.size() >
               name_table.size() * max_load_nom / max_load_denom)
            {
                grow_name_table();
            }
        }
        void grow_name_table();
        int32_t append_entry(int32_t slot_idx);
        int32_t allocate_slot(TValue<String> key, Value initial_value);
        void revive_slot(int32_t slot_idx);

        MemberHeapPtr<Scope> parent_scope;
        RawArray<int32_t> name_table;
        RawArray<Entry> entries;
        ValueArray<Value> slot_values;
        ValueArray<Value> slot_names;
        RawArray<int32_t> slot_current_entry_indices;

    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(
            Scope, parent_scope,
            1 + decltype(name_table)::embedded_value_count +
                decltype(entries)::embedded_value_count +
                decltype(slot_values)::embedded_value_count +
                decltype(slot_names)::embedded_value_count +
                decltype(slot_current_entry_indices)::embedded_value_count);
    };

}  // namespace cl

#endif  // CL_SCOPE_H
