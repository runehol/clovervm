#ifndef CL_SLOT_DICT_H
#define CL_SLOT_DICT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned.h"
#include "typed_value.h"

namespace cl
{
    class ClassObject;
    class String;
    class VirtualMachine;

    class SlotDict : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::SlotDict;

        struct EntryView
        {
            Value key;
            Value value;
        };

        class Iterator
        {
        public:
            EntryView operator*() const;
            Iterator &operator++();
            bool operator==(const Iterator &other) const;
            bool operator!=(const Iterator &other) const;

        private:
            friend class SlotDict;

            Iterator(const SlotDict *dict, uint32_t idx);
            void skip_hidden_entries();

            const SlotDict *dict;
            uint32_t idx;
        };

        SlotDict(ClassObject *cls, Object *target);

        Value get_item(Value key) const;
        Value del_item(Value key);
        Value set_item(Value key, Value value);
        bool contains(Value key) const;

        size_t size() const;
        bool empty() const { return size() == 0; }

        Iterator begin() const;
        Iterator end() const;

        Object *get_target() const { return target.extract(); }
        bool entry_at(uint32_t idx, EntryView &entry) const;

    private:
        static bool key_is_string(Value key);
        static Value key_type_error();

        MemberHeapPtr<Object> target;

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(SlotDict, Object, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(SlotDict);
    };

    BuiltinClassDefinition make_slotdict_class(VirtualMachine *vm);
    void install_slotdict_class_methods(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_SLOT_DICT_H
