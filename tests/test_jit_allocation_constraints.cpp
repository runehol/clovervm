#include "jit/allocation_constraints.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr PhysicalRegister x0(RegisterClass::GPR, 0);
        constexpr PhysicalRegister x1(RegisterClass::GPR, 1);
        constexpr PhysicalRegister x63(RegisterClass::GPR, 63);
        constexpr PhysicalRegister d0(RegisterClass::SIMD, 0);

        SnapshotRef make_empty_snapshot(GraphBuilder &builder)
        {
            return SnapshotRef(builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>{}, BytecodePC{11}));
        }
    }  // namespace

    TEST(JitPhysicalRegister, RegisterSetsKeepClassesIndependent)
    {
        RegisterSet registers;
        registers.insert(x0);
        registers.insert(x63);
        registers.insert(d0);

        EXPECT_TRUE(registers.contains(x0));
        EXPECT_TRUE(registers.contains(x63));
        EXPECT_TRUE(registers.contains(d0));
        EXPECT_FALSE(registers.contains(x1));
        EXPECT_EQ(3u, registers.size());

        registers.erase(x0);
        EXPECT_FALSE(registers.contains(x0));
        EXPECT_TRUE(registers.contains(d0));
        EXPECT_EQ(2u, registers.size());
    }

    TEST(JitPhysicalRegister, RejectsNumbersOutsideTheClassLimit)
    {
        EXPECT_DEATH((void)PhysicalRegister(RegisterClass::GPR, 64),
                     "physical register number exceeds class limit");
        EXPECT_DEATH((void)PhysicalRegister(RegisterClass::Count, 0),
                     "invalid JIT register class");
    }

    TEST(JitPhysicalRegister, ValidatesClassAllocationOrder)
    {
        RegisterSet members;
        members.insert(x0);
        members.insert(x1);
        std::array order = {x1, x0};

        RegisterClassDefinition definition(RegisterClass::GPR, members, order);
        EXPECT_EQ(RegisterClass::GPR, definition.register_class);
        EXPECT_EQ(members, definition.members);
        EXPECT_EQ(x1, definition.allocation_order[0]);

        std::array missing = {x0};
        EXPECT_DEATH(
            (void)RegisterClassDefinition(RegisterClass::GPR, members, missing),
            "does not contain every register exactly once");

        std::array duplicate = {x0, x0};
        EXPECT_DEATH((void)RegisterClassDefinition(RegisterClass::GPR, members,
                                                   duplicate),
                     "contains a duplicate");

        std::array wrong_class = {x0, d0};
        EXPECT_DEATH((void)RegisterClassDefinition(RegisterClass::GPR, members,
                                                   wrong_class),
                     "wrong register class");
    }

    TEST(JitRegisterRequirement, RepresentsAnyFixedAndSameAsInput)
    {
        RegisterRequirement any = RegisterRequirement::any(RegisterClass::SIMD);
        EXPECT_EQ(RegisterRequirement::Kind::Any, any.kind());
        EXPECT_EQ(RegisterClass::SIMD, any.register_class());

        RegisterRequirement fixed = RegisterRequirement::fixed(x63);
        EXPECT_EQ(RegisterRequirement::Kind::Fixed, fixed.kind());
        EXPECT_EQ(x63, fixed.fixed_register());

        RegisterRequirement same = RegisterRequirement::same_as_input(1234);
        EXPECT_EQ(RegisterRequirement::Kind::SameAsInput, same.kind());
        EXPECT_EQ(1234u, same.input_index());
    }

    TEST(JitAllocationConstraints, ValidatesFixedInstructionShape)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        TaggedValueRef lhs(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef rhs(builder.make_instruction<ParameterInstruction>());
        SnapshotRef snapshot = make_empty_snapshot(builder);
        AddSMIInstruction *add =
            builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);

        InstructionAllocationConstraints constraints(
            add, {{1, AccessTiming::Late, RegisterRequirement::fixed(x1)}},
            ResultConstraint{AccessTiming::Late,
                             RegisterRequirement::same_as_input(0)});

        EXPECT_EQ(add, constraints.instruction());
        ASSERT_EQ(1u, constraints.input_overrides().size());
        EXPECT_EQ(1u, constraints.input_overrides()[0].operand_index);
        ASSERT_TRUE(constraints.result_override().has_value());
        EXPECT_EQ(RegisterRequirement::Kind::SameAsInput,
                  constraints.result_override()->requirement.kind());
        EXPECT_EQ(AccessTiming::Late, default_snapshot_use_timing());
        constraints.validate();
    }

    TEST(JitAllocationConstraints, ValidatesVariadicInstructionShape)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        TaggedValueRef callable(
            builder.make_instruction<ParameterInstruction>());
        TaggedValueRef first(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef second(builder.make_instruction<ParameterInstruction>());
        SnapshotRef snapshot = make_empty_snapshot(builder);
        std::array arguments = {first, second};
        PythonCallInstruction *call =
            builder.make_instruction<PythonCallInstruction>(
                callable, snapshot, std::span<const TaggedValueRef>(arguments),
                BytecodePC{19});

        InstructionAllocationConstraints constraints(
            call,
            {{0, AccessTiming::Early, RegisterRequirement::fixed(x0)},
             {2, AccessTiming::Early, RegisterRequirement::fixed(x1)}},
            ResultConstraint{AccessTiming::Late,
                             RegisterRequirement::fixed(x0)});

        EXPECT_EQ(2u, constraints.input_overrides().size());
    }

    TEST(JitAllocationConstraints, AcceptsVirtualSnapshotWithoutDirectUses)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        TaggedValueRef tagged(builder.make_instruction<ParameterInstruction>());
        F64Ref f64(builder.make_instruction<ParameterF64Instruction>());
        std::array<ProgramValueRef, 2> captures = {tagged, f64};
        SnapshotInstruction *snapshot =
            builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>(captures), BytecodePC{23});

        InstructionAllocationConstraints constraints(snapshot);

        EXPECT_TRUE(constraints.input_overrides().empty());
        EXPECT_FALSE(constraints.result_override().has_value());
    }

    TEST(JitAllocationConstraints, MapsValueRepresentationsToRegisterClasses)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        F64Ref lhs(builder.make_instruction<ParameterF64Instruction>());
        F64Ref rhs(builder.make_instruction<ParameterF64Instruction>());
        AddF64Instruction *add =
            builder.make_instruction<AddF64Instruction>(lhs, rhs);

        InstructionAllocationConstraints constraints(
            add, {{1, AccessTiming::Early, RegisterRequirement::fixed(d0)}},
            ResultConstraint{AccessTiming::Late,
                             RegisterRequirement::any(RegisterClass::SIMD)});
        EXPECT_EQ(add, constraints.instruction());

        ProgramValueUseConstraint tagged_default =
            default_program_value_use_constraint(
                7, ValueRepresentation::TaggedValue);
        EXPECT_EQ(AccessTiming::Early, tagged_default.timing);
        EXPECT_EQ(RegisterClass::GPR,
                  tagged_default.requirement.register_class());
        ResultConstraint f64_default =
            default_result_constraint(ValueRepresentation::F64);
        EXPECT_EQ(AccessTiming::Late, f64_default.timing);
        EXPECT_EQ(RegisterClass::SIMD,
                  f64_default.requirement.register_class());

        EXPECT_DEATH((void)InstructionAllocationConstraints(
                         add, {{0, AccessTiming::Early,
                                RegisterRequirement::any(RegisterClass::GPR)}}),
                     "input requirement has the wrong register class");
    }

    TEST(JitAllocationConstraints, AcceptsDefaultsAndRejectsInvalidOverrides)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        TaggedValueRef lhs(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef rhs(builder.make_instruction<ParameterInstruction>());
        SnapshotRef snapshot = make_empty_snapshot(builder);
        AddSMIInstruction *add =
            builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);

        InstructionAllocationConstraints defaults(add);
        EXPECT_TRUE(defaults.input_overrides().empty());
        EXPECT_FALSE(defaults.result_override().has_value());

        EXPECT_DEATH((void)InstructionAllocationConstraints(
                         add,
                         {{0, AccessTiming::Early,
                           RegisterRequirement::any(RegisterClass::GPR)},
                          {0, AccessTiming::Late,
                           RegisterRequirement::any(RegisterClass::GPR)}},
                         std::nullopt),
                     "duplicate input overrides");

        EXPECT_DEATH((void)InstructionAllocationConstraints(
                         add, {{2, AccessTiming::Early,
                                RegisterRequirement::any(RegisterClass::GPR)}}),
                     "does not name an allocatable ProgramValue operand");
    }

    TEST(JitAllocationConstraints, RestrictsSameAsInputToCompatibleResults)
    {
        EXPECT_DEATH(
            (void)ProgramValueUseConstraint(
                0, AccessTiming::Early, RegisterRequirement::same_as_input(0)),
            "input cannot have a SameAsInput");
        EXPECT_DEATH(
            (void)TemporaryConstraint(RegisterRequirement::same_as_input(0)),
            "temporary cannot have a SameAsInput");

        CompilationSession session;
        GraphBuilder builder(session);
        F64Ref source(builder.make_instruction<ParameterF64Instruction>());
        BoxF64Instruction *box =
            builder.make_instruction<BoxF64Instruction>(source);

        EXPECT_DEATH(
            (void)InstructionAllocationConstraints(
                box,
                {{0, AccessTiming::Early,
                  RegisterRequirement::any(RegisterClass::SIMD)}},
                ResultConstraint{AccessTiming::Late,
                                 RegisterRequirement::same_as_input(0)}),
            "different value representation");
    }

    TEST(JitAllocationConstraints, ValidatesClobberCollisions)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        TaggedValueRef source(builder.make_instruction<ParameterInstruction>());
        MovInstruction *move = builder.make_instruction<MovInstruction>(source);
        RegisterSet x0_clobber;
        x0_clobber.insert(x0);

        // A clobber may follow an early fixed use.
        InstructionAllocationConstraints allowed(
            move, {{0, AccessTiming::Early, RegisterRequirement::fixed(x0)}},
            ResultConstraint{AccessTiming::Late,
                             RegisterRequirement::any(RegisterClass::GPR)},
            {}, x0_clobber);
        EXPECT_TRUE(allowed.clobbers().contains(x0));

        EXPECT_DEATH(
            (void)InstructionAllocationConstraints(
                move, {{0, AccessTiming::Late, RegisterRequirement::fixed(x0)}},
                ResultConstraint{AccessTiming::Late,
                                 RegisterRequirement::any(RegisterClass::GPR)},
                {}, x0_clobber),
            "clobber collides with a fixed late input");

        EXPECT_DEATH((void)InstructionAllocationConstraints(
                         move,
                         {{0, AccessTiming::Early,
                           RegisterRequirement::any(RegisterClass::GPR)}},
                         ResultConstraint{AccessTiming::Late,
                                          RegisterRequirement::fixed(x0)},
                         {}, x0_clobber),
                     "clobber collides with a fixed result");

        EXPECT_DEATH(
            (void)InstructionAllocationConstraints(
                move,
                {{0, AccessTiming::Early,
                  RegisterRequirement::any(RegisterClass::GPR)}},
                ResultConstraint{AccessTiming::Late,
                                 RegisterRequirement::any(RegisterClass::GPR)},
                {TemporaryConstraint(RegisterRequirement::fixed(x0))},
                x0_clobber),
            "clobber collides with a fixed temporary");

        EXPECT_DEATH(
            (void)InstructionAllocationConstraints(
                move,
                {{0, AccessTiming::Early, RegisterRequirement::fixed(x0)}},
                ResultConstraint{AccessTiming::Late,
                                 RegisterRequirement::same_as_input(0)},
                {}, x0_clobber),
            "clobber collides with a fixed result");
    }

}  // namespace cl::jit
