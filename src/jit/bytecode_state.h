#ifndef CL_JIT_BYTECODE_STATE_H
#define CL_JIT_BYTECODE_STATE_H

#include "bytecode/bytecode_instruction.h"
#include "bytecode/code_object.h"
#include "runtime/fatal.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    inline bool frame_header_value_is_pointer(int32_t frame_offset)
    {
        assert(frame_offset >= FrameHeaderPreviousFpOffset);
        assert(frame_offset <= FrameHeaderReturnPcOffset);
        return frame_offset != FrameHeaderReturnCodeObjectOffset;
    }

    class BytecodeStateOrder
    {
    public:
        static constexpr size_t AccumulatorPosition = 0;
        static constexpr size_t FirstFramePosition = 1;

        explicit BytecodeStateOrder(const CodeObject &code_object)
            : code_object_(&code_object),
              n_parameters_(code_object.function_signature.n_parameters),
              n_locals_(code_object.n_locals),
              n_temporaries_(code_object.n_temporaries),
              highest_frame_offset_(n_parameters_ == 0
                                        ? FrameHeaderReturnPcOffset
                                        : int32_t(code_object.encode_reg(0))),
              lowest_frame_offset_(-int32_t(n_locals_ + n_temporaries_))
        {
        }

        size_t size() const { return FirstFramePosition + frame_slot_count(); }

        CodeObject *code_object() const
        {
            return const_cast<CodeObject *>(code_object_);
        }

        size_t frame_slot_count() const
        {
            return size_t(highest_frame_offset_ - lowest_frame_offset_) + 1;
        }

        int32_t highest_frame_offset() const { return highest_frame_offset_; }

        int32_t lowest_frame_offset() const { return lowest_frame_offset_; }

        int32_t frame_offset_at(size_t position) const
        {
            if(position < FirstFramePosition || position >= size())
            {
                fatal("JIT bytecode state position is not a stack slot");
            }
            return highest_frame_offset_ -
                   int32_t(position - FirstFramePosition);
        }

        size_t position_for_frame_offset(int32_t frame_offset) const
        {
            if(frame_offset > highest_frame_offset_ ||
               frame_offset < lowest_frame_offset_)
            {
                fatal("JIT bytecode frame offset is outside the state order");
            }
            return FirstFramePosition +
                   size_t(highest_frame_offset_ - frame_offset);
        }

        size_t position_for(BytecodeValueLocation location) const
        {
            if(location.kind == BytecodeValueLocationKind::Accumulator)
            {
                if(location.frame_offset != 0)
                {
                    fatal("JIT bytecode accumulator location has a nonzero "
                          "frame offset");
                }
                return AccumulatorPosition;
            }
            if(location.kind != BytecodeValueLocationKind::StackSlot)
            {
                fatal("JIT bytecode value location has an invalid kind");
            }
            return position_for_frame_offset(location.frame_offset);
        }

        uint32_t n_parameters() const { return n_parameters_; }
        uint32_t n_locals() const { return n_locals_; }
        uint32_t n_temporaries() const { return n_temporaries_; }

    private:
        const CodeObject *code_object_;
        uint32_t n_parameters_;
        uint32_t n_locals_;
        uint32_t n_temporaries_;
        int32_t highest_frame_offset_;
        int32_t lowest_frame_offset_;
    };

    template <typename Ref> class BytecodeStateTracker;

    template <typename Ref> class BytecodeState
    {
    public:
        BytecodeState(const BytecodeState &) = default;
        BytecodeState &operator=(const BytecodeState &) = default;
        BytecodeState(BytecodeState &&) = default;
        BytecodeState &operator=(BytecodeState &&) = default;

    private:
        friend class BytecodeStateTracker<Ref>;

        explicit BytecodeState(std::span<const Ref> values)
            : values_(values.begin(), values.end())
        {
        }

        explicit BytecodeState(std::vector<Ref> values)
            : values_(std::move(values))
        {
        }

        std::vector<Ref> values_;
    };

    template <typename Ref> class BytecodeStateTracker
    {
    public:
        explicit BytecodeStateTracker(const CodeObject &code_object)
            : order_(code_object)
        {
        }

        BytecodeState<Ref> make_entry_state(std::span<const Ref> parameters,
                                            std::span<const Ref> frame_header,
                                            Ref uninitialized_local,
                                            Ref unavailable) const
        {
            if(parameters.size() != order_.n_parameters())
            {
                fatal("JIT bytecode entry state has the wrong parameter "
                      "count");
            }
            if(frame_header.size() != FrameHeaderSize)
            {
                fatal("JIT bytecode entry state has the wrong frame-header "
                      "size");
            }

            std::vector<Ref> values(order_.size(), unavailable);
            for(uint32_t index = 0; index < order_.n_parameters(); ++index)
            {
                values[BytecodeStateOrder::FirstFramePosition + index] =
                    parameters[index];
            }
            for(int32_t frame_offset = FrameHeaderPreviousFpOffset;
                frame_offset <= FrameHeaderReturnPcOffset; ++frame_offset)
            {
                values[order_.position_for_frame_offset(frame_offset)] =
                    frame_header[size_t(frame_offset -
                                        FrameHeaderPreviousFpOffset)];
            }
            for(uint32_t index = 0; index < order_.n_locals(); ++index)
            {
                values[order_.position_for_frame_offset(-int32_t(index) - 1)] =
                    uninitialized_local;
            }
            return BytecodeState<Ref>(std::move(values));
        }

        BytecodeState<Ref>
        make_state_from_block_parameters(std::span<const Ref> parameters) const
        {
            if(parameters.size() != order_.size())
            {
                fatal("JIT bytecode block state has the wrong parameter "
                      "count");
            }
            return BytecodeState<Ref>(parameters);
        }

        Ref value_at(const BytecodeState<Ref> &state,
                     BytecodeValueLocation location) const
        {
            require_compatible_state(state);
            return state.values_[order_.position_for(location)];
        }

        std::vector<Ref>
        read(const BytecodeState<Ref> &state,
             std::span<const BytecodeValueLocation> sources) const
        {
            require_compatible_state(state);
            std::vector<Ref> values;
            values.reserve(sources.size());
            for(BytecodeValueLocation source: sources)
            {
                values.push_back(state.values_[order_.position_for(source)]);
            }
            return values;
        }

        void write(BytecodeState<Ref> &state,
                   std::span<const BytecodeValueLocation> destinations,
                   std::span<const Ref> results) const
        {
            require_compatible_state(state);
            if(destinations.size() != results.size())
            {
                fatal("JIT bytecode state write has mismatched destination "
                      "and result counts");
            }
            for(size_t index = 0; index < destinations.size(); ++index)
            {
                state.values_[order_.position_for(destinations[index])] =
                    results[index];
            }
        }

        std::span<const Ref> values(const BytecodeState<Ref> &state) const
        {
            require_compatible_state(state);
            return state.values_;
        }

        std::span<const Ref> prefix(const BytecodeState<Ref> &state,
                                    size_t size) const
        {
            require_compatible_state(state);
            if(size > order_.size())
            {
                fatal("JIT bytecode state prefix is too large");
            }
            return std::span<const Ref>(state.values_).first(size);
        }

        std::span<const Ref>
        block_arguments(const BytecodeState<Ref> &state) const
        {
            return values(state);
        }

        size_t block_parameter_count() const { return order_.size(); }

        const BytecodeStateOrder &order() const { return order_; }
        uint32_t n_parameters() const { return order_.n_parameters(); }
        uint32_t n_locals() const { return order_.n_locals(); }
        uint32_t n_temporaries() const { return order_.n_temporaries(); }

    private:
        void require_compatible_state(const BytecodeState<Ref> &state) const
        {
            if(state.values_.size() != order_.size())
            {
                fatal("JIT bytecode state has incompatible dimensions");
            }
        }

        BytecodeStateOrder order_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_BYTECODE_STATE_H
