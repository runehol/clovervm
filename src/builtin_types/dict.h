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
    struct TrustedDictBytecodeAccess;

    class Dict : public Object
    {
    private:
        friend struct DictStorageLayoutAssertions;
        friend struct TrustedDictBytecodeAccess;

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

        struct ItemResult
        {
            Value value;
            bool found;
        };

        struct SetDefaultResult
        {
            Value value;
            bool was_present;
        };

        explicit Dict(ClassObject *cls);

        Dict(ClassObject *cls, const Dict &other);

        [[nodiscard]] Expected<Value> get_item(ThreadState *thread, Value key);
        [[nodiscard]] Expected<ItemResult>
        get_item_if_present(ThreadState *thread, Value key);
        [[nodiscard]] Expected<Value> get_item_or_default(ThreadState *thread,
                                                          Value key,
                                                          Value default_value);
        [[nodiscard]] Expected<void> set_item(ThreadState *thread, Value key,
                                              Value value);
        [[nodiscard]] Expected<void> del_item(ThreadState *thread, Value key);
        [[nodiscard]] Expected<bool> contains(ThreadState *thread, Value key);
        [[nodiscard]] Expected<Value> pop(ThreadState *thread, Value key);
        [[nodiscard]] Expected<ItemResult>
        pop_item_if_present(ThreadState *thread, Value key);
        [[nodiscard]] Expected<Value> setdefault(ThreadState *thread, Value key,
                                                 Value default_value);
        [[nodiscard]] Expected<SetDefaultResult>
        setdefault_with_presence(ThreadState *thread, Value key,
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
        int64_t table_generation() const { return table_generation_.extract(); }

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
        [[nodiscard]] Expected<void> update_from_dict(ThreadState *thread,
                                                      const Dict *other);
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
        bool string_keyed_delete_if_present(TValue<String> key);

        struct Probe
        {
            TValue<SMI> hash;
            size_t hash_idx;
            int64_t first_tombstone_idx;
            TValue<SMI> table_generation;
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
            entries.write_value_member(static_cast<size_t>(entry_idx),
                                       &Entry::value, value);
        }
        void copy_stored_entry(Value key, Value value, TValue<SMI> hash)
        {
            resize_general_if_needed();
            uint64_t raw_hash = hash.extract();
            size_t hash_idx = raw_hash & (hash_table.size() - 1);
            while(hash_table[hash_idx] != not_present)
            {
                hash_idx = (hash_idx + 1) & (hash_table.size() - 1);
            }
            write_new_at_slot(hash_idx, hash, key, value);
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
            if(needs_resize_for_insert())
            {
                grow();
            }
        }
        bool needs_resize_for_insert() const
        {
            return entries.size() >
                   hash_table.size() * max_load_nom / max_load_denom;
        }
        bool entry_still_matches(TValue<SMI> generation, size_t hash_idx,
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
        [[nodiscard]] Expected<int64_t>
        find_entry_slot_for_general_lookup(ThreadState *thread, Value key,
                                           TValue<SMI> hash_smi);
        [[nodiscard]] Expected<ItemResult>
        general_get_item_if_present(ThreadState *thread, Value key);
        [[nodiscard]] Expected<void>
        set_item_with_known_hash(ThreadState *thread, Value key, Value value,
                                 TValue<SMI> hash);
        [[nodiscard]] Expected<void> general_set_item(ThreadState *thread,
                                                      Value key, Value value);
        [[nodiscard]] Expected<void> general_del_item(ThreadState *thread,
                                                      Value key);
        [[nodiscard]] Expected<bool> general_contains(ThreadState *thread,
                                                      Value key);
        [[nodiscard]] Expected<ItemResult>
        general_pop_item_if_present(ThreadState *thread, Value key);
        [[nodiscard]] Expected<SetDefaultResult>
        general_setdefault_with_presence(ThreadState *thread, Value key,
                                         Value default_value);
        void promote_to_general_shape(ThreadState *thread);
        void maybe_promote_to_general_shape(ThreadState *thread);
        void grow();
        void increment_table_generation()
        {
            int64_t generation = table_generation_.extract();
            assert(generation < value_smi_max);
            table_generation_ = TValue<SMI>::from_smi(generation + 1);
        }

        RawArray<int32_t> hash_table;
        ValueArray<Entry> entries;
        size_t n_valid_entries;
        TValue<SMI> table_generation_;

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            Dict, Object,
            decltype(hash_table)::embedded_value_count +
                decltype(entries)::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(Dict);
    };

    struct TrustedDictBytecodeAccess
    {
        static constexpr int64_t ReadStringMiss = 0;
        static constexpr int64_t ReadStringHit = 1;
        static constexpr int64_t ReadGeneral = 2;
        static constexpr int64_t SetItemStringDone = 3;
        static constexpr int64_t SetItemGeneral = 4;
        static constexpr int64_t DeleteStringDone = 5;
        static constexpr int64_t DeleteStringMiss = 6;
        static constexpr int64_t DeleteGeneral = 7;
        static constexpr int64_t ProbeMiss = -1;
        static constexpr int64_t ProbeContinue = -2;
        static constexpr int64_t InsertProbeEmpty = -3;
        static constexpr int64_t InsertProbeTombstone = -4;
        static constexpr int64_t InsertProbeHashMiss = -5;

        struct PrepareReadResult
        {
            int64_t status;
            Value value;
        };

        static PrepareReadResult prepare_read(ThreadState *thread, Dict *dict,
                                              Value key);
        static int64_t prepare_set_item(ThreadState *thread, Dict *dict,
                                        Value key, Value value);
        static int64_t prepare_delete(ThreadState *thread, Dict *dict,
                                      Value key);
        static void probe_start(const Dict *dict, TValue<SMI> hash,
                                TValue<SMI> *generation, size_t *hash_idx)
        {
            Dict::Probe probe = dict->probe_start(hash);
            *generation = probe.table_generation;
            *hash_idx = probe.hash_idx;
        }
        static int64_t probe_for_lookup(const Dict *dict, TValue<SMI> hash,
                                        size_t hash_idx)
        {
            assert(hash_idx < dict->hash_table.size());
            int32_t entry_idx = dict->hash_table[hash_idx];
            if(entry_idx == Dict::not_present)
            {
                return ProbeMiss;
            }
            if(entry_idx == Dict::tombstone)
            {
                return ProbeContinue;
            }
            assert(entry_idx >= 0 &&
                   static_cast<size_t>(entry_idx) < dict->entries.size());
            return dict->entries[entry_idx].hash == hash ? entry_idx
                                                         : ProbeContinue;
        }
        static int64_t probe_for_insert(const Dict *dict, TValue<SMI> hash,
                                        size_t hash_idx)
        {
            assert(hash_idx < dict->hash_table.size());
            int32_t entry_idx = dict->hash_table[hash_idx];
            if(entry_idx == Dict::not_present)
            {
                return InsertProbeEmpty;
            }
            if(entry_idx == Dict::tombstone)
            {
                return InsertProbeTombstone;
            }
            assert(entry_idx >= 0 &&
                   static_cast<size_t>(entry_idx) < dict->entries.size());
            return dict->entries[entry_idx].hash == hash ? entry_idx
                                                         : InsertProbeHashMiss;
        }
        static size_t probe_advance(const Dict *dict, size_t hash_idx)
        {
            return (hash_idx + 1) & (dict->hash_table.size() - 1);
        }
        static Value entry_key(const Dict *dict, int32_t entry_idx)
        {
            assert(entry_idx >= 0 &&
                   static_cast<size_t>(entry_idx) < dict->entries.size());
            assert(dict->entries[entry_idx].valid());
            return dict->entries[entry_idx].key;
        }
        static Value entry_value(const Dict *dict, int32_t entry_idx)
        {
            assert(entry_idx >= 0 &&
                   static_cast<size_t>(entry_idx) < dict->entries.size());
            assert(dict->entries[entry_idx].valid());
            return dict->entries[entry_idx].value;
        }
        static bool entry_still_matches(const Dict *dict,
                                        TValue<SMI> generation, size_t hash_idx,
                                        int32_t entry_idx, Value candidate_key)
        {
            return dict->entry_still_matches(generation, hash_idx, entry_idx,
                                             candidate_key);
        }
        static bool needs_resize_for_insert(const Dict *dict)
        {
            return dict->needs_resize_for_insert();
        }
        static void resize_for_insert(Dict *dict)
        {
            dict->resize_general_if_needed();
        }
        static void insert_new(Dict *dict, size_t hash_idx,
                               int64_t first_tombstone_idx, TValue<SMI> hash,
                               Value key, Value value)
        {
            assert(hash_idx < dict->hash_table.size());
            assert(dict->hash_table[hash_idx] == Dict::not_present);
            size_t write_idx = hash_idx;
            if(first_tombstone_idx >= 0)
            {
                size_t tombstone_idx = static_cast<size_t>(first_tombstone_idx);
                if(tombstone_idx < dict->hash_table.size() &&
                   dict->hash_table[tombstone_idx] == Dict::tombstone)
                {
                    write_idx = tombstone_idx;
                }
            }
            dict->write_new_at_slot(write_idx, hash, key, value);
        }
        static void overwrite_entry(Dict *dict, int32_t entry_idx, Value value)
        {
            assert(entry_idx >= 0 &&
                   static_cast<size_t>(entry_idx) < dict->entries.size());
            assert(dict->entries[entry_idx].valid());
            dict->write_existing(entry_idx, value);
        }
        static void delete_entry(Dict *dict, size_t hash_idx)
        {
            assert(hash_idx < dict->hash_table.size());
            assert(dict->hash_table[hash_idx] >= 0);
            dict->delete_entry_at_slot(hash_idx);
        }
    };

    class VirtualMachine;
    BuiltinClassDefinition make_dict_class(VirtualMachine *vm);
    void install_dict_class_methods(VirtualMachine *vm,
                                    ClassObject *type_error_class);

};  // namespace cl

#endif  // CL_DICT_H
