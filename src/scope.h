#ifndef CL_SCOPE_H
#define CL_SCOPE_H

#include <vector>
#include "object.h"
#include "klass.h"
#include "refcount.h"
#include "indirect_dict.h"
#include "value.h"

namespace cl
{

    class SlotEntry
    {
    public:
        SlotEntry(Value _value, Value _extra = Value::None())
        {
            set_value(_value);
            set_extra(_extra);
        }

        ~SlotEntry()
        {
            decref(value);
            decref(extra);
        }

        void set_value(Value _value)
        {
            // note the order, in case val == slots[slot_idx]
            incref(_value);
            decref(value);
            value = _value;
        }

        void set_extra(Value _extra)
        {
            // note the order, in case val == slots[slot_idx]
            incref(_extra);
            decref(extra);
            extra = _extra;
        }

        Value get_value() const
        {
            return value;
        }

        Value get_extra() const
        {
            return extra;
        }
    private:

        Value value;
        Value extra; // extra is used for registering invalidation arrays for global scopes, and closure usage for local scopes
    };

    class Scope : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Scope", nullptr);

        Scope(Value _parent_scope);



        /* For a write, we just insert a regular not-present value with no parent scope slot indication (-1) */
        int32_t register_slot_index_for_write(Value key);

        /* whereas for a read, if the value isn't present, we also
         * register the slot in the parent scope and save that slot in
         * the not-present value. This is to accelerate access to
         * builtins, which live in the parent scope
         */
        int32_t register_slot_index_for_read(Value key);

        int32_t lookup_slot_index_local(Value name) const;


        Value get_by_name(Value name) const;

        ALWAYSINLINE Value get_by_slot_index_fastpath_only(int32_t slot_idx) const
        {
            assert(slot_idx >= 0);
            Value v = slots[slot_idx].get_value();
            if(!v.is_not_present()) return v;
            int32_t parent_slot_idx = v.get_not_present_index();
            if(likely(parent_slot_idx >= 0))
            {
                MUSTTAIL return get_parent_scope_ptr()->get_by_slot_index_fastpath_only(parent_slot_idx);
            } else {
                return Value::not_present();
            }
        }

        NOINLINE Value get_by_slot_index(int32_t slot_idx) const
        {
            assert(slot_idx >= 0);
            Value v = slots[slot_idx].get_value();
            if(!v.is_not_present()) return v;
            int32_t parent_slot_idx = v.get_not_present_index();
            if(likely(parent_slot_idx >= 0))
            {
                return get_parent_scope_ptr()->get_by_slot_index(parent_slot_idx);
            } else {
                if(unlikely(parent_scope != Value::None()))
                {
                    return get_parent_scope_ptr()->get_by_name(indirect_dict.get_key_by_slot_index(slot_idx));
                }
                return Value::not_present();
            }
        }


        void set_by_name(Value name, Value val);

        ALWAYSINLINE void set_by_slot_index(int32_t slot_idx, Value val)
        {
            slots[slot_idx].set_value(val);
        }

        void reserve_empty_slots(size_t n_slots);



        uint32_t size() const { return slots.size(); }
        bool empty() const { return slots.empty(); }

    private:
        Scope *get_parent_scope_ptr() const
        {
            return reinterpret_cast<Scope *>(parent_scope.get_ptr());
        }


        Value parent_scope;
        IndirectDict indirect_dict;
        // TODO these need to be CL arrays at some point, but we're not ready for that yet
        std::vector<SlotEntry> slots;


    };




};



#endif //CL_SCOPE_H
