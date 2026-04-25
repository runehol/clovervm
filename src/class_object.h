#ifndef CL_CLASS_OBJECT_H
#define CL_CLASS_OBJECT_H

#include "klass.h"
#include "object.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "shape.h"
#include "typed_value.h"
#include "value.h"
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject : public Object
    {
    public:
        class MemberEntry
        {
        public:
            MemberEntry(TValue<String> name, Value value)
                : name(name), value(value)
            {
            }

            TValue<String> get_name() const { return name; }
            Value get_value() const { return value.as_value(); }
            void set_value(Value new_value) { value = new_value; }

        private:
            OwnedTValue<String> name;
            OwnedValue value;
        };

        static constexpr Klass klass = Klass(L"Class", nullptr);

        ClassObject(TValue<String> name, uint32_t instance_inline_slot_count,
                    Value base = Value::None());

        TValue<String> get_name() const { return name; }
        uint32_t get_instance_inline_slot_count() const
        {
            return instance_inline_slot_count;
        }
        Shape *get_initial_shape() const;
        ClassObject *get_base() const;
        uint64_t get_method_version() const { return method_version; }

        uint32_t member_count() const { return members.size(); }
        TValue<String> get_member_name(uint32_t member_idx) const
        {
            return members[member_idx].get_name();
        }
        Value get_member_value(uint32_t member_idx) const
        {
            return members[member_idx].get_value();
        }

        Value get_member(TValue<String> name) const;
        void set_member(TValue<String> name, Value value);
        bool delete_member(TValue<String> name);

        Value get_own_property(TValue<String> name) const;
        void set_own_property(TValue<String> name, Value value);
        bool delete_own_property(TValue<String> name);

    private:
        static bool is_method_value(Value value);
        void maybe_bump_method_version_for_write(Value old_value,
                                                 Value new_value);
        int32_t lookup_member_index_local(TValue<String> name) const;

        MemberTValue<String> name;
        uint32_t instance_inline_slot_count;
        uint64_t method_version;
        MemberValue base;
        MemberValue initial_shape;
        std::vector<MemberEntry> members;

    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(ClassObject, name, 3);
    };

}  // namespace cl

#endif  // CL_CLASS_OBJECT_H
