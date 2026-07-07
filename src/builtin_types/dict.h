#ifndef CL_DICT_H
#define CL_DICT_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/typed_value.h"
#include "object_model/vm_array.h"
#include <cstdint>

namespace cl
{
    class ClassObject;
    struct DictStorageLayoutAssertions;
    class List;
    class String;
    class ThreadState;
    class Tuple;

    class Dict : public Object
    {
    private:
        friend struct DictStorageLayoutAssertions;

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

        [[nodiscard]] Expected<Value> get_item(ThreadState *thread, Value key);
        [[nodiscard]] Expected<void> set_item(ThreadState *thread, Value key,
                                              Value value);
        [[nodiscard]] Expected<void> del_item(ThreadState *thread, Value key);
        [[nodiscard]] Expected<bool> contains(ThreadState *thread, Value key);
        [[nodiscard]] Expected<Value> pop(ThreadState *thread, Value key);
        [[nodiscard]] Expected<Value> setdefault(ThreadState *thread, Value key,
                                                 Value default_value);

        [[nodiscard]] Expected<Value> get_item_for_str(ThreadState *thread,
                                                       TValue<String> key);
        [[nodiscard]] Expected<void>
        set_item_for_str(ThreadState *thread, TValue<String> key, Value value);
        [[nodiscard]] Expected<void> del_item_for_str(ThreadState *thread,
                                                      TValue<String> key);
        [[nodiscard]] Expected<bool> contains_for_str(ThreadState *thread,
                                                      TValue<String> key);
        [[nodiscard]] Expected<Value> pop_for_str(ThreadState *thread,
                                                  TValue<String> key);
        [[nodiscard]] Expected<Value> setdefault_for_str(ThreadState *thread,
                                                         TValue<String> key,
                                                         Value default_value);

        size_t size() const { return n_valid_entries; }
        bool empty() const { return n_valid_entries == 0; }
        uint64_t table_generation() const { return table_generation_; }

        Iterator begin() const;
        Iterator end() const;
        size_t entry_storage_size() const { return entries.size(); }
        bool entry_at(size_t idx, EntryView &out) const;

        void clear();
        [[nodiscard]] TValue<Dict> copy() const;
        [[nodiscard]] Value keys();
        [[nodiscard]] Value values();
        [[nodiscard]] Value items();
        [[nodiscard]] Value popitem();
        void update_from_dict(const Dict *other);
        [[nodiscard]] static Value from_tuple_keys(const Tuple *keys,
                                                   Value value);
        [[nodiscard]] static Value from_list_keys(const List *keys,
                                                  Value value);

        // Raw string-keyed storage operations. These are only for VM-owned
        // dictionaries whose shape and contents are known string-keyed, or for
        // trusted handlers after exact receiver/key guards. Python-visible dict
        // operations must use the semantic APIs above.
        [[nodiscard]] Value string_keyed_lookup(TValue<String> key) const;
        void string_keyed_insert(TValue<String> key, Value value);
        [[nodiscard]] Value string_keyed_delete(TValue<String> key);
        bool string_keyed_contains(TValue<String> key) const;

    private:
        constexpr static uint32_t max_load_nom = 3;
        constexpr static uint32_t max_load_denom = 4;
        constexpr static int32_t tombstone = -2;
        constexpr static int32_t not_present = -1;
        constexpr static size_t min_table_size = 16;

        const int32_t *find_entry(TValue<String> key) const;
        const int32_t *
        find_entry_with_provided_hash(TValue<String> key,
                                      TValue<SMI> hash_smi) const;
        int32_t *find_entry(TValue<String> key);
        int32_t *find_entry_with_provided_hash(TValue<String> key,
                                               TValue<SMI> hash_smi);

        struct Probe
        {
            TValue<SMI> hash;
            size_t hash_idx;
            int64_t first_tombstone_idx;
            uint64_t table_generation;
        };

        Probe probe_start(TValue<SMI> hash) const
        {
            return Probe{hash,
                         static_cast<uint64_t>(hash.extract()) &
                             (hash_table.size() - 1),
                         -1, table_generation_};
        }
        void probe_advance(Probe &probe) const
        {
            probe.hash_idx = (probe.hash_idx + 1) & (hash_table.size() - 1);
        }
        void probe_record_tombstone(Probe &probe, int32_t entry_status) const
        {
            assert(entry_status == tombstone);
            if(probe.first_tombstone_idx < 0)
            {
                probe.first_tombstone_idx =
                    static_cast<int64_t>(probe.hash_idx);
            }
        }
        void probe_clear_recorded_tombstone(Probe &probe) const
        {
            probe.first_tombstone_idx = -1;
        }
        bool probe_recorded_tombstone_still_available(const Probe &probe) const
        {
            return probe.first_tombstone_idx >= 0 &&
                   hash_table[static_cast<size_t>(probe.first_tombstone_idx)] ==
                       tombstone;
        }
        size_t probe_write_slot(const Probe &probe) const
        {
            if(probe_recorded_tombstone_still_available(probe))
            {
                return static_cast<size_t>(probe.first_tombstone_idx);
            }
            return probe.hash_idx;
        }
        void write_new_at_slot(size_t hash_idx, TValue<SMI> hash, Value key,
                               Value value)
        {
            int32_t idx = static_cast<int32_t>(entries.size());
            hash_table[hash_idx] = idx;
            entries.emplace_back(key, value, hash);
            ++n_valid_entries;
        }
        void write_existing(int32_t entry_idx, Value value)
        {
            Entry existing = entries[entry_idx];
            entries.set(entry_idx, Entry(existing.key, value, existing.hash));
        }
        void resize_general_if_needed()
        {
            if(entries.size() >
               hash_table.size() * max_load_nom / max_load_denom)
            {
                grow();
            }
        }
        bool entry_still_matches(uint64_t generation, size_t hash_idx,
                                 int32_t entry_idx, Value candidate_key) const
        {
            if(table_generation_ != generation)
            {
                return false;
            }
            if(hash_idx >= hash_table.size() ||
               hash_table[hash_idx] != entry_idx)
            {
                return false;
            }
            if(entry_idx < 0 ||
               static_cast<size_t>(entry_idx) >= entries.size())
            {
                return false;
            }
            const Entry &entry = entries[entry_idx];
            return entry.valid() && entry.key == candidate_key;
        }

        [[nodiscard]] Expected<size_t>
        find_entry_slot_for_general_insert(ThreadState *thread, Value key,
                                           TValue<SMI> hash_smi);
        [[nodiscard]] Expected<int32_t>
        find_entry_index_for_general_lookup(ThreadState *thread, Value key,
                                            TValue<SMI> hash_smi);
        [[nodiscard]] Expected<void> general_set_item(ThreadState *thread,
                                                      Value key, Value value);
        [[nodiscard]] Expected<Value>
        general_setdefault(ThreadState *thread, Value key, Value default_value);
        void always_promote_to_general_shape(ThreadState *thread);
        void maybe_promote_to_general_shape(ThreadState *thread);
        void grow();

        RawArray<int32_t> hash_table;
        ValueArray<Entry> entries;
        size_t n_valid_entries;
        uint64_t table_generation_;

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
        friend struct DictStorageLayoutAssertions;

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

        struct EntryView
        {
            Value key;
            Value value;
        };

        explicit GeneralDict(ClassObject *cls);

        [[nodiscard]] Expected<Value> get_item(ThreadState *thread, Value key);
        [[nodiscard]] Expected<void> set_item(ThreadState *thread, Value key,
                                              Value value);
        [[nodiscard]] Expected<void> del_item(ThreadState *thread, Value key);
        [[nodiscard]] Expected<bool> contains(ThreadState *thread, Value key);
        void clear();

        size_t size() const { return n_valid_entries; }
        bool empty() const { return n_valid_entries == 0; }
        uint64_t table_generation() const { return table_generation_; }
        size_t entry_storage_size() const { return entries.size(); }
        bool entry_at(size_t idx, EntryView &out) const;

    private:
        constexpr static uint32_t max_load_nom = 3;
        constexpr static uint32_t max_load_denom = 4;
        constexpr static int32_t tombstone = -2;
        constexpr static int32_t not_present = -1;
        constexpr static size_t min_table_size = 16;

        struct Probe
        {
            TValue<SMI> hash;
            size_t hash_idx;
            int64_t first_tombstone_idx;
            uint64_t table_generation;
        };

        Probe probe_start(TValue<SMI> hash) const
        {
            return Probe{hash,
                         static_cast<uint64_t>(hash.extract()) &
                             (hash_table.size() - 1),
                         -1, table_generation_};
        }
        void probe_advance(Probe &probe) const
        {
            probe.hash_idx = (probe.hash_idx + 1) & (hash_table.size() - 1);
        }
        void probe_record_tombstone(Probe &probe, int32_t entry_status) const
        {
            assert(entry_status == tombstone);
            if(probe.first_tombstone_idx < 0)
            {
                probe.first_tombstone_idx =
                    static_cast<int64_t>(probe.hash_idx);
            }
        }
        void probe_clear_recorded_tombstone(Probe &probe) const
        {
            probe.first_tombstone_idx = -1;
        }
        bool probe_recorded_tombstone_still_available(const Probe &probe) const
        {
            return probe.first_tombstone_idx >= 0 &&
                   hash_table[static_cast<size_t>(probe.first_tombstone_idx)] ==
                       tombstone;
        }
        size_t probe_write_slot(const Probe &probe) const
        {
            if(probe_recorded_tombstone_still_available(probe))
            {
                return static_cast<size_t>(probe.first_tombstone_idx);
            }
            return probe.hash_idx;
        }
        void write_new_at_slot(size_t hash_idx, TValue<SMI> hash, Value key,
                               Value value)
        {
            int32_t idx = static_cast<int32_t>(entries.size());
            hash_table[hash_idx] = idx;
            entries.emplace_back(key, value, hash);
            ++n_valid_entries;
        }
        void write_existing(int32_t entry_idx, Value value)
        {
            Entry existing = entries[entry_idx];
            entries.set(entry_idx, Entry(existing.key, value, existing.hash));
        }
        void delete_entry_at_slot(size_t hash_idx)
        {
            int32_t entry_idx = hash_table[hash_idx];
            assert(entry_idx >= 0);
            entries.set(entry_idx, Entry(Value::not_present(), Value::None(),
                                         TValue<SMI>::from_smi(0)));
            hash_table[hash_idx] = tombstone;
            --n_valid_entries;
        }
        void resize_general_if_needed()
        {
            if(entries.size() >
               hash_table.size() * max_load_nom / max_load_denom)
            {
                grow();
            }
        }

        [[nodiscard]] Expected<size_t>
        find_entry_slot_for_insert(ThreadState *thread, Value key,
                                   TValue<SMI> hash_smi);
        [[nodiscard]] Expected<int32_t>
        find_entry_index_for_lookup(ThreadState *thread, Value key,
                                    TValue<SMI> hash_smi);
        [[nodiscard]] Expected<int64_t>
        find_entry_slot_for_lookup(ThreadState *thread, Value key,
                                   TValue<SMI> hash_smi);

        bool entry_still_matches(uint64_t generation, size_t hash_idx,
                                 int32_t entry_idx, Value candidate_key) const
        {
            if(table_generation_ != generation)
            {
                return false;
            }
            if(hash_idx >= hash_table.size() ||
               hash_table[hash_idx] != entry_idx)
            {
                return false;
            }
            if(entry_idx < 0 ||
               static_cast<size_t>(entry_idx) >= entries.size())
            {
                return false;
            }
            const Entry &entry = entries[entry_idx];
            return entry.valid() && entry.key == candidate_key;
        }
        void grow();

        RawArray<int32_t> hash_table;
        ValueArray<Entry> entries;
        size_t n_valid_entries;
        uint64_t table_generation_;

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            GeneralDict, Object,
            decltype(hash_table)::embedded_value_count +
                decltype(entries)::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(GeneralDict);
    };

    struct DictStorageLayoutAssertions
    {
        static_assert(
            sizeof(Dict::Entry) == sizeof(GeneralDict::Entry),
            "Dict and GeneralDict entries must stay layout-compatible");
        static_assert(sizeof(Dict::EntryView) == sizeof(GeneralDict::EntryView),
                      "Dict and GeneralDict entry views must stay compatible");
        static_assert(sizeof(Dict) == sizeof(GeneralDict),
                      "Dict and GeneralDict storage members must stay aligned");
    };

    class VirtualMachine;
    BuiltinClassDefinition make_dict_class(VirtualMachine *vm);
    BuiltinClassDefinition make_general_dict_class(VirtualMachine *vm);
    void install_dict_class_methods(VirtualMachine *vm);
    void install_general_dict_class_methods(VirtualMachine *vm);

};  // namespace cl

#endif  // CL_DICT_H
