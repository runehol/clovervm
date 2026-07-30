#include "jit/transition_program_emitter.h"

#include "jit/bytecode_state.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/transition_executor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        uint64_t word_for(Value value)
        {
            static_assert(sizeof(value) == sizeof(uint64_t));
            uint64_t result;
            std::memcpy(&result, &value, sizeof(result));
            return result;
        }

        struct EmitterFixture
        {
            EmitterFixture() : builder(session, IRLevel::Core)
            {
                code_object = context.compile_file(L"pass\n");
                code_object->function_signature.n_parameters = 0;
                code_object->n_locals = 2;
                code_object->n_temporaries = 1;
                state_order =
                    std::make_unique<BytecodeStateOrder>(*code_object);

                captured.emplace_back(
                    builder.make_instruction<ParameterInstruction>());
                while(captured.size() < state_order->size())
                {
                    captured.emplace_back(
                        builder.make_instruction<ParameterInstruction>());
                }

                ExitToInterpreterInstruction exit =
                    builder.make_instruction<ExitToInterpreterInstruction>(
                        captured, BytecodePC{37});
                exit_id = exit.id();
                std::vector<InstructionId> parameter_ids;
                parameter_ids.reserve(captured.size());
                for(ProgramValueRef value: captured)
                {
                    parameter_ids.push_back(value.instruction_id());
                }
                std::array instructions = {exit.id()};
                SideExitRegionId region =
                    builder.make_side_exit_region(parameter_ids, instructions)
                        ->id();
                side_exit_owner.emplace(
                    builder.make_instruction<
                        ResumeInInterpreterWithSideExitInstruction>(captured,
                                                                    region));
            }

            SideExitBinding binding() const
            {
                return make_side_exit_binding(*side_exit_owner);
            }

            test::VmTestContext context;
            CodeObject *code_object = nullptr;
            std::unique_ptr<BytecodeStateOrder> state_order;
            CompilationSession session;
            GraphBuilder builder;
            std::vector<ProgramValueRef> captured;
            std::optional<InstructionId> exit_id;
            std::optional<ResumeInInterpreterWithSideExitInstruction>
                side_exit_owner;
        };

        struct ExecutionStorage
        {
            std::array<Value, 64> stack{};
            Value *frame_pointer = stack.data() + 32;
        };
    }  // namespace

    TEST(TransitionProgramEmitter, PublishesSnapshotStateWithoutMoveScratch)
    {
        EmitterFixture fixture;
        ExecutionStorage execution;
        std::vector<uint64_t> register_file(fixture.state_order->size());
        std::vector<TransitionLocation> input_locations;
        input_locations.reserve(fixture.state_order->size());

        Value accumulator = Value::from_smi(101);
        register_file[BytecodeStateOrder::AccumulatorPosition] =
            word_for(accumulator);
        for(size_t position = 0; position < fixture.state_order->size();
            ++position)
        {
            input_locations.push_back(TransitionLocation::register_file(
                static_cast<int16_t>(position)));
            if(position >= BytecodeStateOrder::FirstFramePosition)
            {
                register_file[position] =
                    word_for(Value::from_smi(static_cast<int64_t>(position)));
            }
        }

        std::vector<TransitionInstruction> program =
            emit_side_exit_transition_program(
                *fixture.session.storage(), *fixture.state_order,
                fixture.binding(), input_locations);

        ASSERT_FALSE(program.empty());
        EXPECT_EQ(1u, program.front().scratch_slot_count());
        TransitionExecutionContext context;
        InterpreterResumeState resume = execute_transition_program(
            context, program, {register_file, execution.frame_pointer});

        EXPECT_EQ(accumulator, resume.accumulator);
        EXPECT_EQ(37u, resume.resume_pc);
        for(size_t position = BytecodeStateOrder::FirstFramePosition;
            position < fixture.state_order->size(); ++position)
        {
            EXPECT_EQ(
                Value::from_smi(static_cast<int64_t>(position)),
                execution.frame_pointer[fixture.state_order->frame_offset_at(
                    position)]);
        }
    }

    TEST(TransitionProgramEmitter, UsesMoveScratchForAFrameCycle)
    {
        EmitterFixture fixture;
        ExecutionStorage execution;
        std::array<uint64_t, 1> register_file = {word_for(Value::from_smi(71))};
        std::vector<TransitionLocation> input_locations = {
            TransitionLocation::register_file(0),
        };

        for(size_t position = BytecodeStateOrder::FirstFramePosition;
            position < fixture.state_order->size(); ++position)
        {
            int32_t destination =
                fixture.state_order->frame_offset_at(position);
            int32_t source = destination;
            if(position == BytecodeStateOrder::FirstFramePosition)
            {
                source = fixture.state_order->frame_offset_at(position + 1);
            }
            else if(position == BytecodeStateOrder::FirstFramePosition + 1)
            {
                source = fixture.state_order->frame_offset_at(position - 1);
            }
            input_locations.push_back(
                TransitionLocation::stack(static_cast<int16_t>(source)));
            execution.frame_pointer[source] =
                Value::from_smi(static_cast<int64_t>(position));
        }

        std::vector<TransitionInstruction> program =
            emit_side_exit_transition_program(
                *fixture.session.storage(), *fixture.state_order,
                fixture.binding(), input_locations);

        EXPECT_EQ(2u, program.front().scratch_slot_count());
        TransitionExecutionContext context;
        InterpreterResumeState resume = execute_transition_program(
            context, program, {register_file, execution.frame_pointer});

        EXPECT_EQ(Value::from_smi(71), resume.accumulator);
        for(size_t position = BytecodeStateOrder::FirstFramePosition;
            position < fixture.state_order->size(); ++position)
        {
            EXPECT_EQ(
                Value::from_smi(static_cast<int64_t>(position)),
                execution.frame_pointer[fixture.state_order->frame_offset_at(
                    position)]);
        }
    }

    TEST(TransitionProgramEmitter, RejectsMismatchedInputLocations)
    {
        EXPECT_DEATH(
            {
                EmitterFixture fixture;
                std::array locations = {TransitionLocation::register_file(0)};
                (void)emit_side_exit_transition_program(
                    *fixture.session.storage(), *fixture.state_order,
                    fixture.binding(), locations);
            },
            "argument locations do not match");
    }

    TEST(TransitionProgramEmitter, RejectsAnUnsupportedRetainedInstruction)
    {
        EXPECT_DEATH(
            ([] {
                EmitterFixture fixture;
                ConstInstruction constant =
                    fixture.builder.make_instruction<ConstInstruction>(
                        Value::None());
                std::array instructions = {constant.id(), *fixture.exit_id};
                std::vector<InstructionId> parameter_ids;
                parameter_ids.reserve(fixture.captured.size());
                for(ProgramValueRef value: fixture.captured)
                {
                    parameter_ids.push_back(value.instruction_id());
                }
                SideExitRegionId region =
                    fixture.builder
                        .make_side_exit_region(parameter_ids, instructions)
                        ->id();
                ResumeInInterpreterWithSideExitInstruction owner =
                    fixture.builder.make_instruction<
                        ResumeInInterpreterWithSideExitInstruction>(
                        fixture.captured, region);
                std::vector<TransitionLocation> locations;
                for(size_t index = 0; index < fixture.captured.size(); ++index)
                {
                    locations.push_back(TransitionLocation::register_file(
                        static_cast<int16_t>(index)));
                }
                (void)emit_side_exit_transition_program(
                    *fixture.session.storage(), *fixture.state_order,
                    make_side_exit_binding(owner), locations);
            }()),
            "unsupported instruction in transition program");
    }

}  // namespace cl::jit
