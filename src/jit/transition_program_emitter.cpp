#include "jit/transition_program_emitter.h"

#include "jit/allocation_constraints.h"
#include "jit/bytecode_state.h"
#include "jit/compilation_storage.h"
#include "jit/parallel_assignment_resolver.h"
#include "jit/side_exit.h"
#include "jit/side_exit_region.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        class SideExitTransitionEmitter
        {
        public:
            SideExitTransitionEmitter(
                const CompilationStorage &storage,
                const BytecodeStateOrder &state_order,
                std::span<const InstructionId> instructions,
                absl::flat_hash_map<InstructionId, TransitionLocation>
                    value_locations)
                : storage_(&storage), state_order_(&state_order),
                  instructions_(instructions),
                  value_locations_(std::move(value_locations))
            {
            }

            std::vector<TransitionInstruction> build() &&
            {
                for(InstructionId id: instructions_)
                {
                    Instruction instruction = storage_->instruction(id);
                    switch(instruction.kind())
                    {
                        case InstructionKind::Snapshot:
                            append_snapshot(
                                instruction.as<SnapshotInstruction>());
                            break;
                        default:
                            fatal("unsupported instruction in transition "
                                  "program");
                    }
                }
                return std::move(builder_).finalize();
            }

        private:
            TransitionLocation location_for(ProgramValueRef value) const
            {
                auto found = value_locations_.find(value.instruction_id());
                if(found == value_locations_.end())
                {
                    fatal("transition instruction value has no location");
                }
                return found->second;
            }

            void append_snapshot(SnapshotInstruction snapshot)
            {
                ProgramValueRefRange captured = snapshot.captured_values();
                if(captured.size() != state_order_->size())
                {
                    fatal("transition Snapshot does not match its bytecode "
                          "state order");
                }

                uint32_t next_scratch =
                    std::max(builder_.next_scratch_slot(), uint32_t{1});
                if(next_scratch >
                   static_cast<uint32_t>(std::numeric_limits<int16_t>::max()))
                {
                    fatal("transition program scratch locations are not "
                          "encodable");
                }
                TransitionLocation accumulator_result =
                    TransitionLocation::scratch(0);
                TransitionLocation move_scratch = TransitionLocation::scratch(
                    static_cast<int16_t>(next_scratch));

                std::vector<ParallelAssignment<TransitionLocation>> assignments;
                assignments.reserve(captured.size());
                auto append_assignment = [&](ProgramValueRef value,
                                             TransitionLocation destination) {
                    ValueRepresentation representation =
                        storage_->instruction(value.instruction_id())
                            .value_representation();
                    assignments.push_back(
                        {location_for(value), destination,
                         register_class_for_representation(representation)});
                };

                append_assignment(
                    captured[BytecodeStateOrder::AccumulatorPosition],
                    accumulator_result);
                for(size_t position = BytecodeStateOrder::FirstFramePosition;
                    position < captured.size(); ++position)
                {
                    int32_t frame_offset =
                        state_order_->frame_offset_at(position);
                    if(frame_offset < std::numeric_limits<int16_t>::min() ||
                       frame_offset > std::numeric_limits<int16_t>::max())
                    {
                        fatal("transition frame location is not encodable");
                    }
                    append_assignment(captured[position],
                                      TransitionLocation::stack(
                                          static_cast<int16_t>(frame_offset)));
                }

                auto ordered = order_parallel_assignments<TransitionLocation>(
                    assignments,
                    [&](RegisterClass,
                        size_t) -> std::optional<TransitionLocation> {
                        return move_scratch;
                    });
                if(!ordered)
                {
                    fatal("transition parallel assignment requires "
                          "unavailable scratch");
                }
                for(const OrderedMove<TransitionLocation> &move:
                    std::move(ordered).value().moves)
                {
                    builder_.emplace_transfer(move.destination,
                                              move.source_location);
                }
                builder_.emplace_resume_interpreter(accumulator_result,
                                                    snapshot.resume_pc());
            }

            const CompilationStorage *storage_;
            const BytecodeStateOrder *state_order_;
            std::span<const InstructionId> instructions_;
            TransitionProgramBuilder builder_;
            absl::flat_hash_map<InstructionId, TransitionLocation>
                value_locations_;
        };

        absl::flat_hash_map<InstructionId, TransitionLocation>
        old_side_exit_value_locations(
            const SideExit &side_exit,
            std::span<const TransitionLocation> input_locations)
        {
            if(input_locations.size() != side_exit.inputs().size())
            {
                fatal("transition program input locations do not match "
                      "side-exit inputs");
            }
            absl::flat_hash_map<InstructionId, TransitionLocation> result;
            for(size_t index = 0; index < side_exit.inputs().size(); ++index)
            {
                result.emplace(side_exit.inputs()[index].instruction_id(),
                               input_locations[index]);
            }
            return result;
        }

        absl::flat_hash_map<InstructionId, TransitionLocation>
        side_exit_binding_value_locations(
            const CompilationStorage &storage, SideExitBinding binding,
            std::span<const TransitionLocation> argument_locations)
        {
            const SideExitRegion &region =
                storage.side_exit_region(binding.region);
            if(binding.arguments.size() != region.parameter_ids().size())
            {
                fatal("transition program binding arguments do not match "
                      "side-exit region parameters");
            }
            if(argument_locations.size() != binding.arguments.size())
            {
                fatal("transition program argument locations do not match "
                      "side-exit binding arguments");
            }

            absl::flat_hash_map<InstructionId, TransitionLocation> result;
            for(size_t index = 0; index < region.parameter_ids().size();
                ++index)
            {
                result.emplace(region.parameter_ids()[index],
                               argument_locations[index]);
            }
            return result;
        }
    }  // namespace

    std::vector<TransitionInstruction> emit_side_exit_transition_program(
        const CompilationStorage &storage,
        const BytecodeStateOrder &state_order, const SideExit &side_exit,
        std::span<const TransitionLocation> input_locations)
    {
        return SideExitTransitionEmitter(
                   storage, state_order, side_exit.instructions(),
                   old_side_exit_value_locations(side_exit, input_locations))
            .build();
    }

    std::vector<TransitionInstruction> emit_side_exit_transition_program(
        const CompilationStorage &storage,
        const BytecodeStateOrder &state_order, SideExitBinding binding,
        std::span<const TransitionLocation> argument_locations)
    {
        const SideExitRegion &region = storage.side_exit_region(binding.region);
        return SideExitTransitionEmitter(
                   storage, state_order, region.instruction_ids(),
                   side_exit_binding_value_locations(storage, binding,
                                                     argument_locations))
            .build();
    }

}  // namespace cl::jit
