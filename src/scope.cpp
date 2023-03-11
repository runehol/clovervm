#include "scope.h"

namespace cl
{


    Scope::Scope(Value _parent_scope)
        : Object(&klass, 1, sizeof(Scope)/8),
          parent_scope(incref(_parent_scope))

    {
    }

        /* For a write, we just insert a regular not-present value with no parent scope slot indication (-1) */
    int32_t Scope::register_slot_index_for_write(Value key)
    {
        int32_t slot_idx = indirect_dict.insert(key);
        if(slot_idx >= int32_t(slots.size()))
        {
            assert(slot_idx == int32_t(slots.size()));
            slots.push_back(Value::not_present(-1));
        }
        return slot_idx;
    }

    /* whereas for a read, if the value isn't present, we also
     * register the slot in the parent scope and save that slot in
     * the not-present value. This is to accelerate access to
     * builtins, which live in the parent scope
     */
    int32_t Scope::register_slot_index_for_read(Value key)
    {
        int32_t slot_idx = indirect_dict.insert(key);
        if(slot_idx >= int32_t(slots.size()))
        {
            assert(slot_idx == int32_t(slots.size()));
            int32_t parent_idx = -1;
            if(parent_scope != Value::None())
            {
                parent_idx = get_parent_scope_ptr()->register_slot_index_for_read(key);
            }
            slots.push_back(Value::not_present(parent_idx));
        }
        return slot_idx;
    }

    int32_t Scope::lookup_slot_index_local(Value name) const
    {
        return indirect_dict.lookup(name);
    }


    Value Scope::get_by_name(Value name) const
    {
        int32_t slot_idx = indirect_dict.lookup(name);
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


    void Scope::set_by_name(Value name, Value val)
    {
        int32_t slot_idx = indirect_dict.insert(name);
        if(slot_idx >= int32_t(slots.size()))
        {
            assert(slot_idx == int32_t(slots.size()));
            slots.push_back(Value::not_present());
        }
        set_by_slot_index(slot_idx, val);
    }


    void Scope::reserve_empty_slots(size_t n_slots)
    {

        indirect_dict.reserve_empty_slots(n_slots);
        for(size_t i = 0; i < n_slots; ++i)
        {
            slots.push_back(Value::not_present());
        }
    }


};
