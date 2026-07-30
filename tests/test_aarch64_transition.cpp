#include "jit/aarch64_transition.h"

#include "jit/aarch64_allocation_constraints.h"
#include "jit/aarch64_cfg_emitter.h"
#include "jit/bytecode_state.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/machine_address_internal.h"
#include "jit/register_allocator.h"
#include "jit/side_exit_lowering.h"
#include "jit/transition_executor.h"
#include "jit_code_cache_test_support.h"
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
        using test_support::CacheAndPlatform;

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

        void unused_side_exit_thunk() {}
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

    TEST(AArch64Transition, DeduplicatesSideExitBindingsDuringAArch64Emission)
    {
        test::VmTestContext vm;
        CodeObject *code_object = vm.compile_file(L"pass\n");
        code_object->function_signature.n_parameters = 1;
        code_object->n_locals = 0;
        code_object->n_temporaries = 0;
        BytecodeStateOrder state_order(*code_object);

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        builder.set_bytecode_state_order(state_order);
        Block *entry = builder.emplace_block();
        ParameterInstruction argument =
            builder.emplace_parameter<ParameterInstruction>(entry);

        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        std::vector<ProgramValueRef> captured(
            state_order.size(), ProgramValueRef(region_parameter));
        SnapshotInstruction snapshot =
            builder.make_instruction<SnapshotInstruction>(captured,
                                                          BytecodePC{31});
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {snapshot.id()};
        SideExitRegionId region =
            builder.make_side_exit_region(parameter_ids, instructions)->id();

        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(argument)};
        InlineTagGuardWithSideExitInstruction first_guard =
            builder.emplace_instruction<InlineTagGuardWithSideExitInstruction>(
                entry, TaggedValueRef(argument), arguments,
                InlineValueClass::SMI, region);
        InlineTagGuardWithSideExitInstruction second_guard =
            builder.emplace_instruction<InlineTagGuardWithSideExitInstruction>(
                entry, TaggedValueRef(argument), arguments,
                InlineValueClass::SMI, region);
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(argument));
        ControlFlowGraph *graph = builder.finalize();

        LocationAssignmentsBuilder assignment_builder;
        assignment_builder.assign(ProgramValueRef(argument),
                                  PhysicalLocation::reg(x(0)));
        assignment_builder.assign(ProgramValueRef(first_guard),
                                  PhysicalLocation::reg(x(1)));
        assignment_builder.assign(ProgramValueRef(second_guard),
                                  PhysicalLocation::reg(x(2)));
        LocationAssignments locations =
            std::move(assignment_builder).finalize();

        std::vector<TransitionInstruction> expected_program =
            emit_aarch64_side_exit_transition_program(
                *graph->storage(), *graph->bytecode_state_order(),
                SideExitBinding{region, first_guard.side_exit_arguments()},
                locations);

        CacheAndPlatform fixture(16);
        MachineAddress side_exit_thunk =
            detail::MachineAddressAccess::from_pointer(
                reinterpret_cast<const void *>(&unused_side_exit_thunk));
        auto emission = emit_aarch64_from_cfg(*graph, locations, *fixture.cache,
                                              side_exit_thunk);
        ASSERT_TRUE(emission);
        PublishedCode code = std::move(emission).value();

        EXPECT_EQ(expected_program.size() * sizeof(TransitionInstruction),
                  code.constant_pool().size());
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
        ParameterInstruction argument =
            builder.emplace_parameter<ParameterInstruction>(entry);

        std::vector<ProgramValueRef> captured(state_order.size(),
                                              ProgramValueRef(argument));
        captured[BytecodeStateOrder::AccumulatorPosition] =
            ProgramValueRef(argument);
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

        std::vector<TransitionLocation> input_locations;
        ProgramValueRefRange arguments = owner.side_exit_arguments();
        for(size_t index = 0; index < arguments.size(); ++index)
        {
            input_locations.push_back(aarch64_transition_location(
                locations.location_for(arguments[index])));
        }

        CacheAndPlatform fixture(16);
        MachineAddress side_exit_thunk =
            detail::MachineAddressAccess::from_pointer(
                reinterpret_cast<const void *>(&unused_side_exit_thunk));
        auto emission = emit_aarch64_from_cfg(*graph, locations, *fixture.cache,
                                              side_exit_thunk);
        ASSERT_TRUE(emission);
        PublishedCode code = std::move(emission).value();
        std::span<const std::byte> program_bytes = code.constant_pool();
        ASSERT_EQ(0u, program_bytes.size() % sizeof(TransitionInstruction));
        std::span<const TransitionInstruction> program(
            reinterpret_cast<const TransitionInstruction *>(
                program_bytes.data()),
            program_bytes.size() / sizeof(TransitionInstruction));

        std::array<uint64_t, AArch64TransitionRegisterSlotCount>
            register_file{};
        std::array<Value, 64> stack{};
        Value *frame_pointer = stack.data() + 32;
        Value expected_value = Value::from_smi(73);
        for(size_t index = 0; index < arguments.size(); ++index)
        {
            InstructionId input = arguments[index].instruction_id();
            ASSERT_EQ(argument.id(), input);
            uint64_t word = word_for(expected_value);
            write_transition_input(input_locations[index], word, register_file,
                                   frame_pointer);
        }

        TransitionExecutionContext execution_context;
        InterpreterResumeState resume = execute_transition_program(
            execution_context, program, {register_file, frame_pointer});

        EXPECT_EQ(expected_value, resume.accumulator);
        EXPECT_EQ(29u, resume.resume_pc);
        for(size_t position = BytecodeStateOrder::FirstFramePosition;
            position < state_order.size(); ++position)
        {
            EXPECT_EQ(expected_value,
                      frame_pointer[state_order.frame_offset_at(position)]);
        }
    }

}  // namespace cl::jit
