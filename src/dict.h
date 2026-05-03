#ifndef CL_DICT_H
#define CL_DICT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "typed_value.h"
#include "vm_array.h"

namespace cl
{
    class ClassObject;

    class Dict : public Object
    {
    private:
        struct Entry
        {
            Entry(Value _key, Value _value, TValue<SMI> _hash)
                : key(_key), value(_value), hash(_hash)
            {
            }
            Value key;
            Value value;
            TValue<SMI> hash;
            bool valid() const { return !key.is_not_present(); }
            void invalidate()
            {
                key = Value::not_present();
                value = Value::None();
                hash = TValue<SMI>::from_smi(0);
            }
        };

    public:
        static constexpr NativeLayoutId native_layout_id = NativeLayoutId::Dict;

        explicit Dict(ClassObject *cls);

        Dict(ClassObject *cls, const Dict &other);

        void set_item(Value key, Value value);
        [[nodiscard]] Value get_item(Value key) const;

        [[nodiscard]] Value del_item(Value key);

        bool contains(Value key) const;

        size_t size() const { return n_valid_entries; }
        bool empty() const { return n_valid_entries == 0; }

        void clear();

    private:
        constexpr static uint32_t max_load_nom = 3;
        constexpr static uint32_t max_load_denom = 4;
        constexpr static int32_t tombstone = -2;
        constexpr static int32_t not_present = -1;
        constexpr static size_t min_table_size = 16;

        const int32_t *find_entry(Value key) const;
        const int32_t *
        find_entry_with_provided_hash(Value key, TValue<SMI> hash_smi) const;
        int32_t *find_entry(Value key);
        int32_t *find_entry_with_provided_hash(Value key, TValue<SMI> hash_smi);

        void grow();

        RawArray<int32_t> hash_table;
        ValueArray<Entry> entries;
        size_t n_valid_entries;

    public:
        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(
            Dict, Object,
            decltype(hash_table)::embedded_value_count +
                decltype(entries)::embedded_value_count);
    };

    class VirtualMachine;
    BuiltinClassDefinition make_dict_class(VirtualMachine *vm);

};  // namespace cl

#endif  // CL_DICT_H
