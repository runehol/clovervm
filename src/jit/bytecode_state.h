#ifndef CL_JIT_BYTECODE_STATE_H
#define CL_JIT_BYTECODE_STATE_H

#include "bytecode/bytecode_instruction.h"
#include "bytecode/code_object.h"
#include "runtime/fatal.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
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

        BytecodeState(Ref accumulator, std::span<const Ref> parameters,
                      size_t n_locals, Ref uninitialized_local,
                      size_t n_temporaries, Ref uninitialized_temporary)
            : accumulator_(std::move(accumulator)),
              parameters_(parameters.begin(), parameters.end()),
              locals_(n_locals, uninitialized_local),
              temporaries_(n_temporaries, uninitialized_temporary)
        {
        }

        BytecodeState(Ref accumulator, std::span<const Ref> parameters,
                      std::span<const Ref> locals,
                      std::span<const Ref> temporaries)
            : accumulator_(std::move(accumulator)),
              parameters_(parameters.begin(), parameters.end()),
              locals_(locals.begin(), locals.end()),
              temporaries_(temporaries.begin(), temporaries.end())
        {
        }

        Ref accumulator_;
        std::vector<Ref> parameters_;
        std::vector<Ref> locals_;
        std::vector<Ref> temporaries_;
    };

    template <typename Ref> class BytecodeStateTracker
    {
    public:
        explicit BytecodeStateTracker(const CodeObject &code_object)
            : n_parameters_(code_object.function_signature.n_parameters),
              n_locals_(code_object.n_locals),
              n_temporaries_(code_object.n_temporaries),
              first_local_register_(code_object.get_padded_n_parameters() +
                                    FrameHeaderSize),
              first_temporary_register_(first_local_register_ + n_locals_)
        {
            size_t n_locations =
                size_t(1) + n_parameters_ + n_locals_ + n_temporaries_;
            block_transfer_locations_.reserve(n_locations);
            block_transfer_locations_.push_back(
                BytecodeValueLocation::accumulator());
            for(uint32_t index = 0; index < n_parameters_; ++index)
            {
                block_transfer_locations_.push_back(
                    {BytecodeValueLocationKind::Parameter, index});
            }
            for(uint32_t index = 0; index < n_locals_; ++index)
            {
                block_transfer_locations_.push_back(
                    {BytecodeValueLocationKind::Local,
                     first_local_register_ + index});
            }
            for(uint32_t index = 0; index < n_temporaries_; ++index)
            {
                block_transfer_locations_.push_back(
                    {BytecodeValueLocationKind::Temporary,
                     first_temporary_register_ + index});
            }
        }

        BytecodeState<Ref> make_entry_state(std::span<const Ref> parameters,
                                            Ref uninitialized_local,
                                            Ref uninitialized_temporary) const
        {
            if(parameters.size() != n_parameters_)
            {
                fatal("JIT bytecode entry state has the wrong parameter "
                      "count");
            }
            return BytecodeState<Ref>(uninitialized_temporary, parameters,
                                      n_locals_, uninitialized_local,
                                      n_temporaries_, uninitialized_temporary);
        }

        BytecodeState<Ref>
        make_state_from_block_parameters(std::span<const Ref> parameters) const
        {
            if(parameters.size() != block_transfer_locations_.size())
            {
                fatal("JIT bytecode block state has the wrong parameter "
                      "count");
            }

            size_t index = 0;
            Ref accumulator = parameters[index++];
            std::span<const Ref> state_parameters =
                parameters.subspan(index, n_parameters_);
            index += n_parameters_;
            std::span<const Ref> locals = parameters.subspan(index, n_locals_);
            index += n_locals_;
            std::span<const Ref> temporaries =
                parameters.subspan(index, n_temporaries_);
            return BytecodeState<Ref>(std::move(accumulator), state_parameters,
                                      locals, temporaries);
        }

        Ref value_at(const BytecodeState<Ref> &state,
                     BytecodeValueLocation location) const
        {
            require_compatible_state(state);
            return value_at_unchecked(state, location);
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
                values.push_back(value_at_unchecked(state, source));
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
                value_at_unchecked(state, destinations[index]) = results[index];
            }
        }

        std::vector<Ref> block_arguments(const BytecodeState<Ref> &state) const
        {
            return read(state, block_transfer_locations_);
        }

        std::span<const BytecodeValueLocation> block_transfer_locations() const
        {
            return block_transfer_locations_;
        }

        size_t block_parameter_count() const
        {
            return block_transfer_locations_.size();
        }

        uint32_t n_parameters() const { return n_parameters_; }
        uint32_t n_locals() const { return n_locals_; }
        uint32_t n_temporaries() const { return n_temporaries_; }

    private:
        void require_compatible_state(const BytecodeState<Ref> &state) const
        {
            if(state.parameters_.size() != n_parameters_ ||
               state.locals_.size() != n_locals_ ||
               state.temporaries_.size() != n_temporaries_)
            {
                fatal("JIT bytecode state has incompatible dimensions");
            }
        }

        template <typename State>
        decltype(auto) value_at_unchecked(State &state,
                                          BytecodeValueLocation location) const
        {
            switch(location.kind)
            {
                case BytecodeValueLocationKind::Accumulator:
                    if(location.register_index != 0)
                    {
                        fatal("JIT bytecode accumulator location has a "
                              "nonzero register index");
                    }
                    return (state.accumulator_);

                case BytecodeValueLocationKind::Parameter:
                    if(location.register_index >= n_parameters_)
                    {
                        fatal("JIT bytecode parameter location is out of "
                              "range");
                    }
                    return state.parameters_[location.register_index];

                case BytecodeValueLocationKind::Local:
                    if(location.register_index < first_local_register_ ||
                       location.register_index - first_local_register_ >=
                           n_locals_)
                    {
                        fatal("JIT bytecode local location is out of range");
                    }
                    return state.locals_[location.register_index -
                                         first_local_register_];

                case BytecodeValueLocationKind::Temporary:
                    if(location.register_index < first_temporary_register_ ||
                       location.register_index - first_temporary_register_ >=
                           n_temporaries_)
                    {
                        fatal("JIT bytecode temporary location is out of "
                              "range");
                    }
                    return state.temporaries_[location.register_index -
                                              first_temporary_register_];
            }
            fatal("JIT bytecode location has an invalid kind");
        }

        uint32_t n_parameters_;
        uint32_t n_locals_;
        uint32_t n_temporaries_;
        uint32_t first_local_register_;
        uint32_t first_temporary_register_;
        std::vector<BytecodeValueLocation> block_transfer_locations_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_BYTECODE_STATE_H
