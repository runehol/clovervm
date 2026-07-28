#include "jit/aarch64_transition.h"

#include "jit/aarch64_allocation_constraints.h"
#include "jit/bytecode_state.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/register_allocator.h"
#include "jit/side_exit_lowering.h"
#include "jit/transition_executor.h"
#include "jit/transition_program_emitter.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr PhysicalRegister x(uint8_t number)
        {
            return PhysicalRegister(RegisterClass::GPR, number);
        }

        constexpr PhysicalRegister d(uint8_t number)
        {
            return PhysicalRegister(RegisterClass::SIMD, number);
        }

        uint64_t word_for(Value value)
        {
            static_assert(sizeof(value) == sizeof(uint64_t));
            uint64_t result;
            std::memcpy(&result, &value, sizeof(result));
            return result;
        }

        uint64_t word_for(ThreadState *thread_state)
        {
            static_assert(sizeof(thread_state) == sizeof(uint64_t));
            uint64_t result;
            std::memcpy(&result, &thread_state, sizeof(result));
            return result;
        }

        void write_transition_input(TransitionLocation location, uint64_t value,
                                    std::span<uint64_t> register_file,
                                    Value *frame_pointer)
        {
            switch(location.area())
            {
                case TransitionLocationArea::RegisterFile:
                    register_file[static_cast<size_t>(location.offset())] =
                        value;
                    return;
                case TransitionLocationArea::Stack:
                    std::memcpy(frame_pointer + location.offset(), &value,
                                sizeof(value));
                    return;
                case TransitionLocationArea::Scratch:
                    break;
            }
            FAIL() << "allocator input unexpectedly mapped to scratch";
        }
    }  // namespace

    TEST(AArch64Transition, MapsPhysicalLocationsIntoSavedState)
    {
        EXPECT_EQ(TransitionLocation::register_file(0),
                  aarch64_transition_location(PhysicalLocation::reg(x(0))));
        EXPECT_EQ(TransitionLocation::register_file(30),
                  aarch64_transition_location(PhysicalLocation::reg(x(30))));
        EXPECT_EQ(TransitionLocation::register_file(32),
                  aarch64_transition_location(PhysicalLocation::reg(d(0))));
        EXPECT_EQ(TransitionLocation::register_file(63),
                  aarch64_transition_location(PhysicalLocation::reg(d(31))));
        EXPECT_EQ(TransitionLocation::stack(-17),
                  aarch64_transition_location(PhysicalLocation::stack(
                      StackLocation(StackLocationKind::SpillSlot, -17))));
        EXPECT_EQ(
            TransitionLocation::stack(23),
            aarch64_transition_location(PhysicalLocation::stack(
                StackLocation(StackLocationKind::IncomingParameter, 23))));
        EXPECT_EQ(64u, AArch64TransitionRegisterSlotCount);
    }

    TEST(AArch64Transition,
         EmitsAndExecutesSnapshotAfterAArch64RegisterAllocation)
    {
        test::VmTestContext vm;
        CodeObject *code_object = vm.compile_file(L"pass\n");
        code_object->function_signature.n_parameters = 1;
        code_object->n_locals = 0;
        code_object->n_temporaries = 0;
        BytecodeStateOrder state_order(*code_object);

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        builder.set_bytecode_state_order(state_order);
        Block *entry = builder.emplace_block();
        ParameterPointerInstruction thread_state =
            builder.emplace_parameter<ParameterPointerInstruction>(entry);
        ParameterInstruction argument =
            builder.emplace_parameter<ParameterInstruction>(entry);

        std::vector<ProgramValueRef> captured(state_order.size(),
                                              ProgramValueRef(argument));
        captured[BytecodeStateOrder::AccumulatorPosition] =
            ProgramValueRef(argument);
        captured[BytecodeStateOrder::ThreadStatePosition] =
            ProgramValueRef(thread_state);
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(entry, captured,
                                                             BytecodePC{29});
        builder.emplace_instruction<ResumeInInterpreterInstruction>(
            entry, SnapshotRef(snapshot));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        auto lowering = lower_side_exits(session, *graph, sunk);
        ASSERT_TRUE(lowering);

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
        auto allocation = allocate_registers(session, *graph, constraints);
        ASSERT_TRUE(allocation);
        LocationAssignments locations = std::move(allocation).value();

        ResumeInInterpreterWithSideExitInstruction owner =
            entry->instruction_at(entry->instructions().size() - 1)
                .as<ResumeInInterpreterWithSideExitInstruction>();
        const SideExit &side_exit = graph->side_exit(owner.side_exit());
        ASSERT_EQ(side_exit.inputs().size(),
                  owner.side_exit_arguments().size());

        std::vector<TransitionLocation> input_locations;
        ProgramValueRefRange arguments = owner.side_exit_arguments();
        for(size_t index = 0; index < arguments.size(); ++index)
        {
            input_locations.push_back(aarch64_transition_location(
                locations.location_for(arguments[index])));
        }

        std::vector<TransitionInstruction> program =
            emit_side_exit_transition_program(*graph->storage(),
                                              *graph->bytecode_state_order(),
                                              side_exit, input_locations);

        std::array<uint64_t, AArch64TransitionRegisterSlotCount>
            register_file{};
        std::array<Value, 64> stack{};
        Value *frame_pointer = stack.data() + 32;
        ThreadState *expected_thread_state =
            reinterpret_cast<ThreadState *>(stack.data());
        Value expected_value = Value::from_smi(73);
        for(size_t index = 0; index < side_exit.inputs().size(); ++index)
        {
            InstructionId input = side_exit.inputs()[index].instruction_id();
            uint64_t word;
            if(input == argument.id())
            {
                word = word_for(expected_value);
            }
            else
            {
                ASSERT_EQ(thread_state.id(), input);
                word = word_for(expected_thread_state);
            }
            write_transition_input(input_locations[index], word, register_file,
                                   frame_pointer);
        }

        TransitionExecutionContext execution_context;
        InterpreterResumeState resume = execute_transition_program(
            execution_context, program, {register_file, frame_pointer});

        EXPECT_EQ(expected_value, resume.accumulator);
        EXPECT_EQ(expected_thread_state, resume.thread_state);
        EXPECT_EQ(29u, resume.resume_pc);
        for(size_t position = BytecodeStateOrder::FirstFramePosition;
            position < state_order.size(); ++position)
        {
            EXPECT_EQ(expected_value,
                      frame_pointer[state_order.frame_offset_at(position)]);
        }
    }

}  // namespace cl::jit
