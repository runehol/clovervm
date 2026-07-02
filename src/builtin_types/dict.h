#ifndef CL_DICT_H
#define CL_DICT_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/typed_value.h"
#include "object_model/vm_array.h"

namespace cl
{
    class ClassObject;
    class List;
    class Tuple;

    class Dict : public Object
    {
    private:
        class Entry
        {
        public:
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
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Dict;

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
            friend class Dict;

            Iterator(const Dict *dict, size_t idx);
            void skip_dead_entries();

            const Dict *dict;
            size_t idx;
        };

        explicit Dict(ClassObject *cls);

        Dict(ClassObject *cls, const Dict &other);

        void set_item(Value key, Value value);
        [[nodiscard]] Value get_item(Value key) const;

        [[nodiscard]] Value del_item(Value key);

        bool contains(Value key) const;

        size_t size() const { return n_valid_entries; }
        bool empty() const { return n_valid_entries == 0; }

        Iterator begin() const;
        Iterator end() const;
        size_t entry_storage_size() const { return entries.size(); }
        bool entry_at(size_t idx, EntryView &out) const;

        void clear();
        [[nodiscard]] TValue<Dict> copy() const;
        [[nodiscard]] Value keys();
        [[nodiscard]] Value values();
        [[nodiscard]] Value items();
        [[nodiscard]] Value pop(Value key);
        [[nodiscard]] Value popitem();
        [[nodiscard]] Value setdefault(Value key, Value default_value);
        void update_from_dict(const Dict *other);
        [[nodiscard]] static Value from_tuple_keys(const Tuple *keys,
                                                   Value value);
        [[nodiscard]] static Value from_list_keys(const List *keys,
                                                  Value value);

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
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            Dict, Object,
            decltype(hash_table)::embedded_value_count +
                decltype(entries)::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(Dict);
    };

    class GeneralDict : public Object
    {
    private:
        class Entry
        {
        public:
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
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::GeneralDict;

        explicit GeneralDict(ClassObject *cls);

        size_t size() const { return n_valid_entries; }
        bool empty() const { return n_valid_entries == 0; }

    private:
        constexpr static uint32_t max_load_nom = 3;
        constexpr static uint32_t max_load_denom = 4;
        constexpr static int32_t tombstone = -2;
        constexpr static int32_t not_present = -1;
        constexpr static size_t min_table_size = 16;

        RawArray<int32_t> hash_table;
        ValueArray<Entry> entries;
        size_t n_valid_entries;

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            GeneralDict, Object,
            decltype(hash_table)::embedded_value_count +
                decltype(entries)::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(GeneralDict);
    };

    class VirtualMachine;
    BuiltinClassDefinition make_dict_class(VirtualMachine *vm);
    BuiltinClassDefinition make_general_dict_class(VirtualMachine *vm);
    void install_dict_class_methods(VirtualMachine *vm);
    void install_general_dict_class_methods(VirtualMachine *vm);

};  // namespace cl

#endif  // CL_DICT_H
