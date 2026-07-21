#ifndef CL_JIT_MACHINE_CODE_EMITTER_H
#define CL_JIT_MACHINE_CODE_EMITTER_H

#include "jit/code_cache.h"
#include "object_model/owned.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cl::jit
{
    template <typename DirectBranch, typename Relocation>
    class MachineCodeEmitter;

    class Label
    {
    public:
        Label() = delete;

    private:
        template <typename DirectBranch, typename Relocation>
        friend class MachineCodeEmitter;

        explicit Label(uint32_t index) : index_(index) {}

        uint32_t index_;
    };

    class ValuePoolEntry
    {
    public:
        ValuePoolEntry() = delete;

    private:
        template <typename DirectBranch, typename Relocation>
        friend class MachineCodeEmitter;

        explicit ValuePoolEntry(size_t byte_offset) : byte_offset_(byte_offset)
        {
        }

        size_t byte_offset_;
    };

    using CodeTarget = std::variant<Label, MachineAddress>;
    using RelocationTarget = ValuePoolEntry;

    template <typename DirectBranch, typename Relocation>
    class MachineCodeEmitter
    {
    public:
        explicit MachineCodeEmitter(size_t maximum_pool_span)
            : maximum_pool_span_(maximum_pool_span)
        {
            assert(maximum_pool_span != 0);
            assert(maximum_pool_span <= DirectBranch::MaximumUnitSize);
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

        void emit_relocatable(const void *bytes, size_t size,
                              Relocation relocation)
        {
            assert(size != 0);
            assert(current_fragment().bytes.size() <=
                   std::numeric_limits<uint32_t>::max());
            uint32_t offset =
                static_cast<uint32_t>(current_fragment().bytes.size());
            emit_bytes(bytes, size);
            current_fragment().relocations.emplace_back(offset,
                                                        std::move(relocation));
        }

        void emit_direct_branch(DirectBranch branch)
        {
            assert(!finalization_attempted_);
            assert(!current_fragment().direct_branch.has_value());
            current_fragment().direct_branch.emplace(std::move(branch));
            fragments_.emplace_back();
        }

        ValuePoolEntry add_value_to_constant_pool(Value value)
        {
            assert(!finalization_attempted_);
            uint64_t raw_value = uint64_t(value.as.integer);
            auto existing = value_indices_by_raw_value_.find(raw_value);
            if(existing != value_indices_by_raw_value_.end())
            {
                return ValuePoolEntry(existing->second * sizeof(Value));
            }

            assert(values_.size() <=
                   std::numeric_limits<size_t>::max() / sizeof(Value));
            size_t index = values_.size();
            values_.emplace_back(value);
            value_indices_by_raw_value_.emplace(raw_value, index);
            return ValuePoolEntry(index * sizeof(Value));
        }

        [[nodiscard]] Result<CodeAllocation, JitCodeError>
        finalize(CodeCache &cache)
        {
            assert(!finalization_attempted_);
            finalization_attempted_ = true;
            size_t pessimistic_size = calculate_pessimistic_layout();
            assert(pessimistic_size != 0);
            assert(pessimistic_size <= DirectBranch::MaximumUnitSize);

            if(!cache.fits_within_span(pessimistic_size, values_.size(),
                                       maximum_pool_span_))
            {
                return Result<CodeAllocation, JitCodeError>::error(
                    JitCodeError::PoolOutOfRange);
            }

            CodeAllocationProposal proposal =
                CL_TRY(cache.propose(pessimistic_size, values_.size()));

            size_t final_size = select_direct_branches(proposal.code_address());
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
            std::optional<DirectBranch> direct_branch;
            size_t pessimistic_start = 0;
            size_t final_start = 0;
        };

        Fragment &current_fragment() { return fragments_.back(); }

        static size_t add_sizes(size_t left, size_t right)
        {
            assert(right <= std::numeric_limits<size_t>::max() - left);
            return left + right;
        }

        size_t calculate_pessimistic_layout()
        {
            for(const std::optional<size_t> &binding: label_bindings_)
            {
                assert(binding.has_value());
            }

            size_t cursor = 0;
            for(Fragment &fragment: fragments_)
            {
                fragment.pessimistic_start = cursor;
                cursor = add_sizes(cursor, fragment.bytes.size());
                if(fragment.direct_branch)
                {
                    uint32_t minimum = fragment.direct_branch->min_size();
                    uint32_t maximum = fragment.direct_branch->max_size();
                    assert(minimum <= maximum);
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

        size_t select_direct_branches(MachineAddress code_address)
        {
            size_t cursor = 0;
            for(Fragment &fragment: fragments_)
            {
                fragment.final_start = cursor;
                cursor = add_sizes(cursor, fragment.bytes.size());
                if(!fragment.direct_branch)
                {
                    continue;
                }

                const CodeTarget &target = fragment.direct_branch->target();
                uint32_t selected;
                if(const Label *label = std::get_if<Label>(&target))
                {
                    size_t pessimistic_branch_offset = add_sizes(
                        fragment.pessimistic_start, fragment.bytes.size());
                    selected = fragment.direct_branch->select(
                        code_address.offset_by(pessimistic_branch_offset),
                        pessimistic_label_address(*label, code_address));
                }
                else
                {
                    selected = fragment.direct_branch->select(
                        code_address.offset_by(cursor),
                        std::get<MachineAddress>(target));
                }
                assert(selected >= fragment.direct_branch->min_size());
                assert(selected <= fragment.direct_branch->max_size());
                cursor = add_sizes(cursor, selected);
            }
            return cursor;
        }

        MachineAddress
        resolve_relocation_target(RelocationTarget target,
                                  MachineAddress pool_address) const
        {
            assert(target.byte_offset_ < values_.size() * sizeof(Value));
            assert(target.byte_offset_ % sizeof(Value) == 0);
            return pool_address.offset_by(target.byte_offset_);
        }

        void encode(const CodeAllocation &allocation) const
        {
            auto *write_base =
                static_cast<uint8_t *>(allocation.write_pointer());
            MachineAddress code_address = allocation.code.execute_address();
            MachineAddress pool_address = allocation.value_pool.address();

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

                if(fragment.direct_branch)
                {
                    size_t branch_offset =
                        add_sizes(fragment.final_start, fragment.bytes.size());
                    MachineAddress target = final_target_address(
                        fragment.direct_branch->target(), code_address);
                    fragment.direct_branch->encode(
                        write_base + branch_offset,
                        code_address.offset_by(branch_offset), target);
                }
            }

            assert(allocation.value_pool.slot_count() == values_.size());
            Value *pool = allocation.value_pool.write_pointer();
            for(size_t index = 0; index < values_.size(); ++index)
            {
                pool[index] = values_[index].value();
            }
        }

        std::vector<Fragment> fragments_;
        std::vector<std::optional<size_t>> label_bindings_;
        std::vector<Owned<Value>> values_;
        std::unordered_map<uint64_t, size_t> value_indices_by_raw_value_;
        size_t maximum_pool_span_;
        bool finalization_attempted_ = false;
    };

}  // namespace cl::jit

#endif  // CL_JIT_MACHINE_CODE_EMITTER_H
