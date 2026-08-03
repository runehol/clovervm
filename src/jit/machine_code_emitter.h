#ifndef CL_JIT_MACHINE_CODE_EMITTER_H
#define CL_JIT_MACHINE_CODE_EMITTER_H

#include "jit/code_cache.h"
#include "object_model/owned.h"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cl::jit
{
    template <typename Relaxation, typename Relocation>
    class MachineCodeEmitter;

    class Label
    {
    public:
        Label() = delete;

    private:
        template <typename Relaxation, typename Relocation>
        friend class MachineCodeEmitter;

        explicit Label(uint32_t index) : index_(index) {}

        uint32_t index_;
    };

    class ConstantPoolEntry
    {
    public:
        ConstantPoolEntry() = delete;

    private:
        template <typename Relaxation, typename Relocation>
        friend class MachineCodeEmitter;

        enum class Area : uint8_t
        {
            TaggedValues,
            UntaggedData,
        };

        ConstantPoolEntry(Area area, size_t byte_offset)
            : area_(area), byte_offset_(byte_offset)
        {
        }

        Area area_;
        size_t byte_offset_;
    };

    using CodeTarget = std::variant<Label, MachineAddress>;
    using RelocationTarget = ConstantPoolEntry;

    template <typename Relaxation, typename Relocation> class MachineCodeEmitter
    {
    public:
        explicit MachineCodeEmitter(size_t maximum_pool_span)
            : maximum_pool_span_(maximum_pool_span)
        {
            assert(maximum_pool_span != 0);
            assert(maximum_pool_span <= Relaxation::MaximumUnitSize);
            fragments_.emplace_back();
        }

        Label make_label()
        {
            assert(!finalization_attempted_);
            assert(label_bindings_.size() <
                   std::numeric_limits<uint32_t>::max());
            Label result(static_cast<uint32_t>(label_bindings_.size()));
            label_bindings_.emplace_back();
            return result;
        }

        void resolve(Label label)
        {
            assert(!finalization_attempted_);
            assert(label.index_ < label_bindings_.size());
            assert(!label_bindings_[label.index_].has_value());
            if(!current_fragment().bytes.empty())
            {
                fragments_.emplace_back();
            }
            label_bindings_[label.index_] = fragments_.size() - 1;
        }

        void emit_bytes(const void *bytes, size_t size)
        {
            assert(!finalization_attempted_);
            if(size == 0)
            {
                return;
            }
            assert(bytes != nullptr);
            const auto *begin = static_cast<const uint8_t *>(bytes);
            current_fragment().bytes.insert(current_fragment().bytes.end(),
                                            begin, begin + size);
        }

        void add_relocation_to_last_emitted(size_t size, Relocation relocation)
        {
            assert(size != 0);
            assert(size <= current_fragment().bytes.size());
            assert(current_fragment().bytes.size() <=
                   std::numeric_limits<uint32_t>::max());
            uint32_t offset =
                static_cast<uint32_t>(current_fragment().bytes.size() - size);
            current_fragment().relocations.emplace_back(offset,
                                                        std::move(relocation));
        }

        void emit_relaxation(Relaxation relaxation)
        {
            assert(!finalization_attempted_);
            assert(!current_fragment().relaxation.has_value());
            current_fragment().relaxation.emplace(std::move(relaxation));
            fragments_.emplace_back();
        }

        ConstantPoolEntry add_value_to_constant_pool(Value value)
        {
            assert(!finalization_attempted_);
            uint64_t raw_value = uint64_t(value.as.integer);
            auto existing = value_indices_by_raw_value_.find(raw_value);
            if(existing != value_indices_by_raw_value_.end())
            {
                return ConstantPoolEntry(ConstantPoolEntry::Area::TaggedValues,
                                         existing->second * sizeof(Value));
            }

            assert(values_.size() <=
                   std::numeric_limits<size_t>::max() / sizeof(Value));
            size_t index = values_.size();
            values_.emplace_back(value);
            value_indices_by_raw_value_.emplace(raw_value, index);
            return ConstantPoolEntry(ConstantPoolEntry::Area::TaggedValues,
                                     index * sizeof(Value));
        }

        ConstantPoolEntry add_heap_object_to_constant_pool(HeapObject *object)
        {
            assert(object != nullptr);
            Value value;
            value.as.ptr = object;
            assert(value.is_ptr());
            return add_value_to_constant_pool(value);
        }

        ConstantPoolEntry
        add_data_to_constant_pool(std::span<const std::byte> data)
        {
            assert(!finalization_attempted_);
            assert(!data.empty());
            size_t offset = align_size(untagged_data_.size(), alignof(Value));
            untagged_data_.resize(offset);
            untagged_data_.insert(untagged_data_.end(), data.begin(),
                                  data.end());
            return ConstantPoolEntry(ConstantPoolEntry::Area::UntaggedData,
                                     offset);
        }

        size_t tagged_value_count() const { return values_.size(); }

        [[nodiscard]] Result<CodeAllocation, JitCodeError>
        finalize(CodeCache &cache)
        {
            assert(!finalization_attempted_);
            finalization_attempted_ = true;
            size_t pessimistic_size = calculate_pessimistic_layout();
            assert(pessimistic_size != 0);
            assert(pessimistic_size <= Relaxation::MaximumUnitSize);

            assert(values_.size() <=
                   std::numeric_limits<size_t>::max() / sizeof(Value));
            size_t tagged_size = values_.size() * sizeof(Value);
            size_t constant_pool_size =
                add_sizes(tagged_size, untagged_data_.size());
            if(!cache.fits_within_span(pessimistic_size, constant_pool_size,
                                       alignof(Value), maximum_pool_span_))
            {
                return Result<CodeAllocation, JitCodeError>::error(
                    JitCodeError::PoolOutOfRange);
            }

            CodeAllocationProposal proposal = CL_TRY(cache.propose(
                pessimistic_size, constant_pool_size, alignof(Value)));

            size_t final_size = select_relaxations(proposal.code_address());
            CodeAllocation allocation = CL_TRY(proposal.commit(final_size));
            encode(allocation);
            return Result<CodeAllocation, JitCodeError>::ok(
                std::move(allocation));
        }

    private:
        struct RelocationEntry
        {
            RelocationEntry(uint32_t offset, Relocation relocation)
                : offset(offset), relocation(std::move(relocation))
            {
            }

            uint32_t offset;
            Relocation relocation;
        };

        struct Fragment
        {
            std::vector<uint8_t> bytes;
            std::vector<RelocationEntry> relocations;
            std::optional<Relaxation> relaxation;
            size_t pessimistic_start = 0;
            size_t final_start = 0;
        };

        Fragment &current_fragment() { return fragments_.back(); }

        static size_t add_sizes(size_t left, size_t right)
        {
            assert(right <= std::numeric_limits<size_t>::max() - left);
            return left + right;
        }

        static size_t align_size(size_t value, size_t alignment)
        {
            assert(std::has_single_bit(alignment));
            size_t mask = alignment - 1;
            assert(value <= std::numeric_limits<size_t>::max() - mask);
            return (value + mask) & ~mask;
        }

        size_t calculate_pessimistic_layout()
        {
            for(const std::optional<size_t> &binding: label_bindings_)
            {
                (void)binding;
                assert(binding.has_value());
            }

            size_t cursor = 0;
            for(Fragment &fragment: fragments_)
            {
                fragment.pessimistic_start = cursor;
                cursor = add_sizes(cursor, fragment.bytes.size());
                if(fragment.relaxation)
                {
                    uint32_t maximum = fragment.relaxation->max_size();
                    assert(fragment.relaxation->min_size() <= maximum);
                    cursor = add_sizes(cursor, maximum);
                }
            }
            return cursor;
        }

        const Fragment &label_fragment(Label label) const
        {
            assert(label.index_ < label_bindings_.size());
            const std::optional<size_t> &binding =
                label_bindings_[label.index_];
            assert(binding.has_value());
            assert(*binding < fragments_.size());
            return fragments_[*binding];
        }

        MachineAddress
        pessimistic_label_address(Label label,
                                  MachineAddress code_address) const
        {
            return code_address.offset_by(
                label_fragment(label).pessimistic_start);
        }

        MachineAddress final_label_address(Label label,
                                           MachineAddress code_address) const
        {
            return code_address.offset_by(label_fragment(label).final_start);
        }

        MachineAddress final_target_address(const CodeTarget &target,
                                            MachineAddress code_address) const
        {
            if(const Label *label = std::get_if<Label>(&target))
            {
                return final_label_address(*label, code_address);
            }
            return std::get<MachineAddress>(target);
        }

        size_t select_relaxations(MachineAddress code_address)
        {
            size_t cursor = 0;
            for(Fragment &fragment: fragments_)
            {
                fragment.final_start = cursor;
                cursor = add_sizes(cursor, fragment.bytes.size());
                if(!fragment.relaxation)
                {
                    continue;
                }

                const CodeTarget &target = fragment.relaxation->target();
                uint32_t selected;
                if(const Label *label = std::get_if<Label>(&target))
                {
                    size_t pessimistic_branch_offset = add_sizes(
                        fragment.pessimistic_start, fragment.bytes.size());
                    selected = fragment.relaxation->select(
                        code_address.offset_by(pessimistic_branch_offset),
                        pessimistic_label_address(*label, code_address));
                }
                else
                {
                    selected = fragment.relaxation->select(
                        code_address.offset_by(cursor),
                        std::get<MachineAddress>(target));
                }
                assert(selected >= fragment.relaxation->min_size());
                assert(selected <= fragment.relaxation->max_size());
                cursor = add_sizes(cursor, selected);
            }
            return cursor;
        }

        MachineAddress
        resolve_relocation_target(RelocationTarget target,
                                  MachineAddress pool_address) const
        {
            size_t tagged_size = values_.size() * sizeof(Value);
            switch(target.area_)
            {
                case ConstantPoolEntry::Area::TaggedValues:
                    assert(target.byte_offset_ < tagged_size);
                    assert(target.byte_offset_ % sizeof(Value) == 0);
                    return pool_address.offset_by(target.byte_offset_);
                case ConstantPoolEntry::Area::UntaggedData:
                    assert(target.byte_offset_ < untagged_data_.size());
                    return pool_address.offset_by(
                        add_sizes(tagged_size, target.byte_offset_));
            }
            assert(false);
            return pool_address;
        }

        void encode(CodeAllocation &allocation) const
        {
            auto *write_base =
                reinterpret_cast<uint8_t *>(allocation.writable_code().data());
            MachineAddress code_address = allocation.code.execute_address();
            MachineAddress pool_address = allocation.constant_pool_address();

            for(const Fragment &fragment: fragments_)
            {
                uint8_t *fragment_write = write_base + fragment.final_start;
                if(!fragment.bytes.empty())
                {
                    std::memcpy(fragment_write, fragment.bytes.data(),
                                fragment.bytes.size());
                }

                for(const RelocationEntry &entry: fragment.relocations)
                {
                    assert(entry.offset < fragment.bytes.size());
                    size_t instruction_offset =
                        add_sizes(fragment.final_start, entry.offset);
                    MachineAddress target = resolve_relocation_target(
                        entry.relocation.target(), pool_address);
                    entry.relocation.apply(
                        write_base + instruction_offset,
                        code_address.offset_by(instruction_offset), target);
                }

                if(fragment.relaxation)
                {
                    size_t branch_offset =
                        add_sizes(fragment.final_start, fragment.bytes.size());
                    MachineAddress target = final_target_address(
                        fragment.relaxation->target(), code_address);
                    fragment.relaxation->encode(
                        write_base + branch_offset,
                        code_address.offset_by(branch_offset), target);
                }
            }

            std::span<std::byte> pool_bytes = allocation.constant_pool();
            size_t tagged_size = values_.size() * sizeof(Value);
            assert(pool_bytes.size() ==
                   add_sizes(tagged_size, untagged_data_.size()));
            auto *pool = reinterpret_cast<Value *>(pool_bytes.data());
            assert(reinterpret_cast<uintptr_t>(pool) % alignof(Value) == 0);
            for(size_t index = 0; index < values_.size(); ++index)
            {
                pool[index] = values_[index].value();
            }
            if(!untagged_data_.empty())
            {
                std::memcpy(pool_bytes.data() + tagged_size,
                            untagged_data_.data(), untagged_data_.size());
            }
        }

        std::vector<Fragment> fragments_;
        std::vector<std::optional<size_t>> label_bindings_;
        std::vector<Owned<Value>> values_;
        std::unordered_map<uint64_t, size_t> value_indices_by_raw_value_;
        std::vector<std::byte> untagged_data_;
        size_t maximum_pool_span_;
        bool finalization_attempted_ = false;
    };

}  // namespace cl::jit

#endif  // CL_JIT_MACHINE_CODE_EMITTER_H
