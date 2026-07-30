#include "jit/aarch64_allocation_constraints.h"

#include "jit/compilation_session.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace cl::jit
{
    namespace
    {
        constexpr PhysicalRegister x(uint8_t number)
        {
            return PhysicalRegister(RegisterClass::GPR, number);
        }

        constexpr PhysicalRegister v(uint8_t number)
        {
            return PhysicalRegister(RegisterClass::SIMD, number);
        }

        const InstructionAllocationConstraints *
        find_override(const AllocationConstraints &constraints,
                      Instruction instruction)
        {
            for(const InstructionAllocationConstraints &candidate:
                constraints.instruction_overrides())
            {
                if(candidate.instruction_id() == instruction.id())
                {
                    return &candidate;
                }
            }
            return nullptr;
        }

        TaggedValueRef emplace_constant(GraphBuilder &builder, Block *block,
                                        Value value = Value::None())
        {
            return TaggedValueRef(
                builder.emplace_instruction<ConstInstruction>(block, value));
        }

        SideExitRegionId make_single_argument_region(GraphBuilder &builder,
                                                     ProgramValueRef argument,
                                                     BytecodePC pc)
        {
            EXPECT_EQ(ValueRepresentation::TaggedValue,
                      builder.storage()
                          ->instruction(argument.instruction_id())
                          .value_representation());
            ParameterInstruction parameter =
                builder.make_instruction<ParameterInstruction>();
            std::array<ProgramValueRef, 1> captured = {
                ProgramValueRef(parameter)};
            ExitToInterpreterInstruction exit =
                builder.make_instruction<ExitToInterpreterInstruction>(captured,
                                                                       pc);
            std::array<InstructionId, 1> parameters = {parameter.id()};
            std::array<InstructionId, 1> instructions = {exit.id()};
            return builder.make_side_exit_region(parameters, instructions)
                ->id();
        }

    }  // namespace

    TEST(AArch64AllocationConstraints,
         DefinesInitialGPRClassAndPlatformEntryABI)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction third =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(third));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

        ASSERT_EQ(2u, constraints.register_classes().size());
        const RegisterClassDefinition &gprs = constraints.register_classes()[0];
        EXPECT_EQ(RegisterClass::GPR, gprs.register_class());
        EXPECT_EQ(16u, gprs.members().size());
        EXPECT_TRUE(gprs.members().contains(x(0)));
        EXPECT_TRUE(gprs.members().contains(x(15)));
        EXPECT_FALSE(gprs.members().contains(x(16)));
        ASSERT_EQ(16u, gprs.allocation_order().size());
        EXPECT_EQ(x(0), gprs.allocation_order().front());
        EXPECT_EQ(x(15), gprs.allocation_order().back());
        ASSERT_EQ(2u, gprs.scratch_registers().size());
        EXPECT_EQ(x(16), gprs.scratch_registers()[0]);
        EXPECT_EQ(x(17), gprs.scratch_registers()[1]);

        const RegisterClassDefinition &simd = constraints.register_classes()[1];
        EXPECT_EQ(RegisterClass::SIMD, simd.register_class());
        EXPECT_EQ(22u, simd.members().size());
        EXPECT_TRUE(simd.members().contains(v(0)));
        EXPECT_TRUE(simd.members().contains(v(7)));
        EXPECT_FALSE(simd.members().contains(v(8)));
        EXPECT_FALSE(simd.members().contains(v(15)));
        EXPECT_TRUE(simd.members().contains(v(16)));
        EXPECT_FALSE(simd.members().contains(v(30)));
        EXPECT_FALSE(simd.members().contains(v(31)));
        ASSERT_EQ(22u, simd.allocation_order().size());
        EXPECT_EQ(v(0), simd.allocation_order().front());
        EXPECT_EQ(v(29), simd.allocation_order().back());
        ASSERT_EQ(2u, simd.scratch_registers().size());
        EXPECT_EQ(v(30), simd.scratch_registers()[0]);
        EXPECT_EQ(v(31), simd.scratch_registers()[1]);

        ASSERT_EQ(4u, constraints.instruction_overrides().size());
        std::array<Instruction, 3> parameters = {first, second, third};
        for(size_t index = 0; index < parameters.size(); ++index)
        {
            const InstructionAllocationConstraints *parameter =
                find_override(constraints, parameters[index]);
            ASSERT_NE(nullptr, parameter);
            ASSERT_TRUE(parameter->result_override().has_value());
            EXPECT_EQ(x(static_cast<uint8_t>(index)),
                      parameter->result_override()
                          ->requirement.fixed_location()
                          .reg());
        }

        const InstructionAllocationConstraints *return_override =
            find_override(constraints, return_instruction);
        ASSERT_NE(nullptr, return_override);
        ASSERT_EQ(1u, return_override->input_overrides().size());
        EXPECT_EQ(ReturnInstruction::return_value_operand_index,
                  return_override->input_overrides()[0].operand_index);
        EXPECT_EQ(x(0), return_override->input_overrides()[0]
                            .requirement.fixed_location()
                            .reg());
    }

    TEST(AArch64AllocationConstraints, OmitsOrdinaryInstructionsAndBranches)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        Block *if_true = builder.emplace_block();
        Block *if_false = builder.emplace_block();
        ParameterInstruction condition =
            builder.emplace_parameter<ParameterInstruction>(entry);
        BlockEdge *true_edge = builder.make_block_edge(entry, if_true);
        BlockEdge *false_edge = builder.make_block_edge(entry, if_false);

        TaggedValueRef lhs =
            emplace_constant(builder, entry, Value::from_smi(0b1010));
        TaggedValueRef rhs =
            emplace_constant(builder, entry, Value::from_smi(0b1100));
        AndSMIInstruction and_instruction =
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs);
        OrrSMIInstruction orr_instruction =
            builder.emplace_instruction<OrrSMIInstruction>(entry, lhs, rhs);
        EorSMIInstruction eor_instruction =
            builder.emplace_instruction<EorSMIInstruction>(entry, lhs, rhs);
        ConditionalBranchInstruction branch =
            builder.emplace_instruction<ConditionalBranchInstruction>(
                entry, TaggedValueRef(condition), true_edge, false_edge);
        ReturnInstruction true_return =
            builder.emplace_instruction<ReturnInstruction>(
                if_true, emplace_constant(builder, if_true, Value::True()));
        ReturnInstruction false_return =
            builder.emplace_instruction<ReturnInstruction>(
                if_false, emplace_constant(builder, if_false, Value::False()));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

        EXPECT_EQ(nullptr,
                  find_override(constraints, graph->storage()->instruction(
                                                 lhs.instruction_id())));
        EXPECT_EQ(nullptr,
                  find_override(constraints, graph->storage()->instruction(
                                                 rhs.instruction_id())));
        EXPECT_EQ(nullptr, find_override(constraints, and_instruction));
        EXPECT_EQ(nullptr, find_override(constraints, orr_instruction));
        EXPECT_EQ(nullptr, find_override(constraints, eor_instruction));

        EXPECT_EQ(nullptr, find_override(constraints, branch));
        EXPECT_NE(nullptr, find_override(constraints, true_return));
        EXPECT_NE(nullptr, find_override(constraints, false_return));
    }

    TEST(AArch64AllocationConstraints, ObservesSideExitArgumentsLate)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction value =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(value)};
        SideExitRegionId region = make_single_argument_region(
            builder, ProgramValueRef(value), BytecodePC{17});
        ResumeInInterpreterWithSideExitInstruction owner =
            builder.emplace_instruction<
                ResumeInInterpreterWithSideExitInstruction>(entry, arguments,
                                                            region);
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
        const InstructionAllocationConstraints *owner_override =
            find_override(constraints, owner);
        ASSERT_NE(nullptr, owner_override);
        ASSERT_EQ(arguments.size(), owner_override->input_overrides().size());
        for(size_t index = 0; index < arguments.size(); ++index)
        {
            const ProgramValueUseConstraint &input =
                owner_override->input_overrides()[index];
            EXPECT_EQ(index, input.operand_index);
            EXPECT_EQ(AccessTiming::Late, input.timing);
            EXPECT_EQ(LocationRequirement::Kind::AnyRegister,
                      input.requirement.kind());
            EXPECT_EQ(RegisterClass::GPR, input.requirement.register_class());
        }
    }

    TEST(AArch64AllocationConstraints,
         ObservesInlineTagGuardSideExitArgumentsLate)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction value =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(value)};
        SideExitRegionId region = make_single_argument_region(
            builder, ProgramValueRef(value), BytecodePC{17});
        InlineTagGuardWithSideExitInstruction owner =
            builder.emplace_instruction<InlineTagGuardWithSideExitInstruction>(
                entry, TaggedValueRef(value), arguments, InlineValueClass::SMI,
                region);
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(owner));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
        const InstructionAllocationConstraints *owner_override =
            find_override(constraints, owner);
        ASSERT_NE(nullptr, owner_override);
        ASSERT_EQ(1u, owner_override->input_overrides().size());
        const ProgramValueUseConstraint &argument =
            owner_override->input_overrides()[0];
        EXPECT_EQ(InlineTagGuardWithSideExitInstruction::
                      side_exit_arguments_operand_index,
                  argument.operand_index);
        EXPECT_EQ(AccessTiming::Late, argument.timing);
        EXPECT_EQ(LocationRequirement::Kind::AnyRegister,
                  argument.requirement.kind());
        EXPECT_EQ(RegisterClass::GPR, argument.requirement.register_class());
    }

    TEST(AArch64AllocationConstraints, ObservesAddSMISideExitArgumentsLate)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(lhs)};
        SideExitRegionId region = make_single_argument_region(
            builder, ProgramValueRef(lhs), BytecodePC{19});
        AddSMIWithSideExitInstruction owner =
            builder.emplace_instruction<AddSMIWithSideExitInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs), arguments,
                region);
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(owner));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
        const InstructionAllocationConstraints *owner_override =
            find_override(constraints, owner);
        ASSERT_NE(nullptr, owner_override);
        ASSERT_EQ(1u, owner_override->input_overrides().size());
        const ProgramValueUseConstraint &argument =
            owner_override->input_overrides()[0];
        EXPECT_EQ(
            AddSMIWithSideExitInstruction::side_exit_arguments_operand_index,
            argument.operand_index);
        EXPECT_EQ(AccessTiming::Late, argument.timing);
        EXPECT_EQ(LocationRequirement::Kind::AnyRegister,
                  argument.requirement.kind());
        EXPECT_EQ(RegisterClass::GPR, argument.requirement.register_class());
    }

    TEST(AArch64AllocationConstraints, GivesIdentityTestsOneGPRTemporary)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        IsInstruction is = builder.emplace_instruction<IsInstruction>(
            entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        IsNotInstruction is_not = builder.emplace_instruction<IsNotInstruction>(
            entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(is_not));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
        for(Instruction instruction:
            {static_cast<Instruction>(is), static_cast<Instruction>(is_not)})
        {
            const InstructionAllocationConstraints *override =
                find_override(constraints, instruction);
            ASSERT_NE(nullptr, override);
            ASSERT_EQ(1u, override->temporaries().size());
            EXPECT_EQ(LocationRequirement::Kind::AnyRegister,
                      override->temporaries().front().requirement.kind());
            EXPECT_EQ(
                RegisterClass::GPR,
                override->temporaries().front().requirement.register_class());
        }
    }

    TEST(AArch64AllocationConstraints,
         InternalBlockParametersUseDefaultConstraints)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction argument =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(argument)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        UnconditionalBranchInstruction branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                exit, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

        EXPECT_EQ(nullptr, find_override(constraints, parameter));
        EXPECT_EQ(nullptr, find_override(constraints, branch));
        EXPECT_NE(nullptr, find_override(constraints, return_instruction));
    }

    TEST(AArch64AllocationConstraints, RejectsUnsupportedInstruction)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        MovInstruction move =
            builder.emplace_instruction<MovInstruction>(entry, parameter);
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(move));
        ControlFlowGraph *graph = builder.finalize();

        EXPECT_DEATH((void)make_aarch64_allocation_constraints(*graph),
                     "unsupported instruction");
    }

}  // namespace cl::jit
