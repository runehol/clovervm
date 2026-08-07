#include "jit/allocation_constraints.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "test_helpers.h"

#include <absl/container/flat_hash_map.h>
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

        LocationRequirement fixed(PhysicalRegister reg)
        {
            return LocationRequirement::fixed(PhysicalLocation::reg(reg));
        }

        SnapshotRef make_empty_snapshot(GraphBuilder &builder)
        {
            return SnapshotRef(builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>{}, BytecodePCOffset{11}));
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

        RegisterClassDefinition definition(RegisterClass::GPR, order);
        EXPECT_EQ(RegisterClass::GPR, definition.register_class());
        EXPECT_EQ(members, definition.members());
        EXPECT_EQ(x1, definition.allocation_order()[0]);
        EXPECT_TRUE(definition.scratch_registers().empty());

        std::array scratch = {x63};
        RegisterClassDefinition with_scratch(RegisterClass::GPR, order,
                                             scratch);
        ASSERT_EQ(1u, with_scratch.scratch_registers().size());
        EXPECT_EQ(x63, with_scratch.scratch_registers()[0]);

        std::array duplicate = {x0, x0};
        EXPECT_DEATH(
            (void)RegisterClassDefinition(RegisterClass::GPR, duplicate),
            "contains a duplicate");

        std::array wrong_class = {x0, d0};
        EXPECT_DEATH(
            (void)RegisterClassDefinition(RegisterClass::GPR, wrong_class),
            "wrong register class");
        EXPECT_DEATH((void)RegisterClassDefinition(RegisterClass::GPR, order,
                                                   std::array{d0}),
                     "scratch register has the wrong");
        EXPECT_DEATH((void)RegisterClassDefinition(RegisterClass::GPR, order,
                                                   std::array{x0}),
                     "scratch register is also allocatable");
        EXPECT_DEATH((void)RegisterClassDefinition(RegisterClass::GPR, order,
                                                   std::array{x63, x63}),
                     "scratch register list contains a duplicate");
    }

    TEST(JitPhysicalRegister, RejectsDuplicateClassDefinitions)
    {
        std::array order = {x0, x1};
        std::vector<RegisterClassDefinition> definitions;
        definitions.emplace_back(RegisterClass::GPR, order);
        definitions.emplace_back(RegisterClass::GPR, order);

        EXPECT_DEATH((void)AllocationConstraints(std::move(definitions), {}),
                     "duplicate JIT register class definition");
    }

    TEST(JitStackLocation, KeepsSemanticKindSeparateFromPhysicalIdentity)
    {
        StackLocation incoming(StackLocationKind::IncomingParameter, 7);
        StackLocation outgoing(StackLocationKind::OutgoingCallArgument, 7);
        StackLocation local(StackLocationKind::LocalOrTemporary, -3);
        StackLocation spill(StackLocationKind::SpillSlot, -19);

        EXPECT_EQ(StackLocationKind::IncomingParameter, incoming.kind());
        EXPECT_EQ(StackLocationKind::OutgoingCallArgument, outgoing.kind());
        EXPECT_EQ(StackLocationKind::LocalOrTemporary, local.kind());
        EXPECT_EQ(StackLocationKind::SpillSlot, spill.kind());
        EXPECT_EQ(7, incoming.frame_offset());
        EXPECT_EQ(-19, spill.frame_offset());
        EXPECT_TRUE(incoming.aliases(outgoing));
        EXPECT_FALSE(incoming.aliases(local));
    }

    TEST(JitPhysicalLocation, RepresentsRegistersAndSemanticStackLocations)
    {
        PhysicalLocation reg = PhysicalLocation::reg(x0);
        PhysicalLocation same_reg = PhysicalLocation::reg(x0);
        PhysicalLocation other_reg = PhysicalLocation::reg(x1);
        PhysicalLocation incoming = PhysicalLocation::stack(
            StackLocation(StackLocationKind::IncomingParameter, 5));
        PhysicalLocation outgoing = PhysicalLocation::stack(
            StackLocation(StackLocationKind::OutgoingCallArgument, 5));

        EXPECT_TRUE(reg.is_register());
        EXPECT_FALSE(reg.is_stack());
        EXPECT_EQ(x0, reg.reg());
        EXPECT_EQ(reg, same_reg);
        EXPECT_NE(reg, other_reg);
        EXPECT_TRUE(reg.aliases(same_reg));
        EXPECT_FALSE(reg.aliases(other_reg));
        EXPECT_FALSE(reg.aliases(incoming));
        EXPECT_TRUE(incoming.is_stack());
        EXPECT_EQ(StackLocationKind::IncomingParameter,
                  incoming.stack().kind());
        EXPECT_EQ(incoming, outgoing);
        EXPECT_TRUE(incoming.aliases(outgoing));

        absl::flat_hash_map<PhysicalLocation, int> locations;
        locations.emplace(reg, 1);
        locations.emplace(incoming, 2);
        EXPECT_EQ(1, locations.at(same_reg));
        EXPECT_EQ(2, locations.at(outgoing));
    }

    TEST(JitLocationRequirement,
         RepresentsAnyLocationFixedOperandCopyAndSameAsInput)
    {
        LocationRequirement any_location = LocationRequirement::any_location();
        EXPECT_EQ(LocationRequirement::Kind::AnyLocation, any_location.kind());

        LocationRequirement any =
            LocationRequirement::any_register(RegisterClass::SIMD);
        EXPECT_EQ(LocationRequirement::Kind::AnyRegister, any.kind());
        EXPECT_EQ(RegisterClass::SIMD, any.register_class());

        LocationRequirement fixed_register = fixed(x63);
        EXPECT_EQ(LocationRequirement::Kind::FixedLocation,
                  fixed_register.kind());
        EXPECT_EQ(x63, fixed_register.fixed_location().reg());

        LocationRequirement fixed_stack =
            LocationRequirement::fixed(PhysicalLocation::stack(
                StackLocation(StackLocationKind::OutgoingCallArgument, -17)));
        EXPECT_EQ(LocationRequirement::Kind::FixedLocation, fixed_stack.kind());
        EXPECT_EQ(-17, fixed_stack.fixed_location().stack().frame_offset());

        LocationRequirement fixed_operand_copy =
            LocationRequirement::fixed_operand_copy(x63);
        EXPECT_EQ(LocationRequirement::Kind::FixedOperandCopy,
                  fixed_operand_copy.kind());
        EXPECT_EQ(x63, fixed_operand_copy.fixed_operand_copy_register());

        LocationRequirement same = LocationRequirement::same_as_input(1234);
        EXPECT_EQ(LocationRequirement::Kind::SameAsInput, same.kind());
        EXPECT_EQ(1234u, same.input_index());
    }

    TEST(JitAllocationConstraints,
         AcceptsFixedStackValuesButRequiresRegisterTemporaries)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef source(builder.make_instruction<ParameterInstruction>());
        MovInstruction move = builder.make_instruction<MovInstruction>(source);
        LocationRequirement outgoing =
            LocationRequirement::fixed(PhysicalLocation::stack(
                StackLocation(StackLocationKind::OutgoingCallArgument, -8)));
        LocationRequirement local =
            LocationRequirement::fixed(PhysicalLocation::stack(
                StackLocation(StackLocationKind::LocalOrTemporary, -2)));

        InstructionAllocationConstraints constraints(
            move,
            {{0, AccessTiming::Early, LocationRequirement::any_location()}},
            ResultConstraint{AccessTiming::Late, local});
        constraints.validate(*session.storage());

        EXPECT_DEATH((void)TemporaryConstraint(outgoing),
                     "temporary requires a register location");
        EXPECT_DEATH(
            (void)TemporaryConstraint(LocationRequirement::any_location()),
            "temporary cannot accept any location");
        EXPECT_DEATH((void)TemporaryConstraint(
                         LocationRequirement::fixed_operand_copy(x0)),
                     "temporary cannot have a FixedOperandCopy");
        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move, {},
                    ResultConstraint{AccessTiming::Late,
                                     LocationRequirement::any_location()});
                invalid.validate(*session.storage());
            },
            "result cannot accept any location");
        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move, {},
                    ResultConstraint{
                        AccessTiming::Late,
                        LocationRequirement::fixed_operand_copy(x0)});
                invalid.validate(*session.storage());
            },
            "result cannot have a FixedOperandCopy");
    }

    TEST(JitAllocationConstraints, ValidatesFixedInstructionShape)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef lhs(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef rhs(builder.make_instruction<ParameterInstruction>());
        SnapshotRef snapshot = make_empty_snapshot(builder);
        AddSMIInstruction add =
            builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);

        InstructionAllocationConstraints constraints(
            add, {{1, AccessTiming::Late, fixed(x1)}},
            ResultConstraint{AccessTiming::Late,
                             LocationRequirement::same_as_input(0)});

        EXPECT_EQ(add.id(), constraints.instruction_id());
        ASSERT_EQ(1u, constraints.input_overrides().size());
        EXPECT_EQ(1u, constraints.input_overrides()[0].operand_index);
        ASSERT_TRUE(constraints.result_override().has_value());
        EXPECT_EQ(LocationRequirement::Kind::SameAsInput,
                  constraints.result_override()->requirement.kind());
        EXPECT_EQ(AccessTiming::Late, default_snapshot_use_timing());
        constraints.validate(*session.storage());
    }

    TEST(JitAllocationConstraints, ValidatesVariadicInstructionShape)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef callable(
            builder.make_instruction<ParameterInstruction>());
        TaggedValueRef first(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef second(builder.make_instruction<ParameterInstruction>());
        SnapshotRef snapshot = make_empty_snapshot(builder);
        std::array arguments = {first, second};
        PythonCallInstruction call =
            builder.make_instruction<PythonCallInstruction>(
                callable, snapshot, std::span<const TaggedValueRef>(arguments),
                BytecodePCOffset{19});

        InstructionAllocationConstraints constraints(
            call,
            {{0, AccessTiming::Early, fixed(x0)},
             {2, AccessTiming::Early, fixed(x1)}},
            ResultConstraint{AccessTiming::Late, fixed(x0)});

        EXPECT_EQ(2u, constraints.input_overrides().size());
    }

    TEST(JitAllocationConstraints, AcceptsVirtualSnapshotWithoutDirectUses)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef tagged(builder.make_instruction<ParameterInstruction>());
        F64Ref f64(builder.make_instruction<ParameterF64Instruction>());
        std::array<ProgramValueRef, 2> captures = {tagged, f64};
        SnapshotInstruction snapshot =
            builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>(captures),
                BytecodePCOffset{23});

        InstructionAllocationConstraints constraints(snapshot);

        EXPECT_TRUE(constraints.input_overrides().empty());
        EXPECT_FALSE(constraints.result_override().has_value());
    }

    TEST(JitAllocationConstraints, MapsValueRepresentationsToRegisterClasses)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        F64Ref lhs(builder.make_instruction<ParameterF64Instruction>());
        F64Ref rhs(builder.make_instruction<ParameterF64Instruction>());
        AddF64Instruction add =
            builder.make_instruction<AddF64Instruction>(lhs, rhs);

        InstructionAllocationConstraints constraints(
            add, {{1, AccessTiming::Early, fixed(d0)}},
            ResultConstraint{
                AccessTiming::Late,
                LocationRequirement::any_register(RegisterClass::SIMD)});
        EXPECT_EQ(add.id(), constraints.instruction_id());

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
        ResultConstraint pointer_default =
            default_result_constraint(ValueRepresentation::Pointer);
        EXPECT_EQ(AccessTiming::Late, pointer_default.timing);
        EXPECT_EQ(RegisterClass::GPR,
                  pointer_default.requirement.register_class());

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    add,
                    {{0, AccessTiming::Early,
                      LocationRequirement::any_register(RegisterClass::GPR)}});
                invalid.validate(*session.storage());
            },
            "input requirement has the wrong register class");
    }

    TEST(JitAllocationConstraints, AcceptsDefaultsAndRejectsInvalidOverrides)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef lhs(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef rhs(builder.make_instruction<ParameterInstruction>());
        SnapshotRef snapshot = make_empty_snapshot(builder);
        AddSMIInstruction add =
            builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);

        InstructionAllocationConstraints defaults(add);
        EXPECT_TRUE(defaults.input_overrides().empty());
        EXPECT_FALSE(defaults.result_override().has_value());

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    add,
                    {{0, AccessTiming::Early,
                      LocationRequirement::any_register(RegisterClass::GPR)},
                     {0, AccessTiming::Late,
                      LocationRequirement::any_register(RegisterClass::GPR)}},
                    std::nullopt);
                invalid.validate(*session.storage());
            },
            "duplicate input overrides");

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    add,
                    {{2, AccessTiming::Early,
                      LocationRequirement::any_register(RegisterClass::GPR)}});
                invalid.validate(*session.storage());
            },
            "does not name an allocatable ProgramValue operand");
    }

    TEST(JitAllocationConstraints, RestrictsSameAsInputToCompatibleResults)
    {
        EXPECT_DEATH(
            (void)ProgramValueUseConstraint(
                0, AccessTiming::Early, LocationRequirement::same_as_input(0)),
            "input cannot have a SameAsInput");
        EXPECT_DEATH(
            (void)TemporaryConstraint(LocationRequirement::same_as_input(0)),
            "temporary cannot have a SameAsInput");

        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        F64Ref source(builder.make_instruction<ParameterF64Instruction>());
        BoxF64Instruction box =
            builder.make_instruction<BoxF64Instruction>(source);

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    box,
                    {{0, AccessTiming::Early,
                      LocationRequirement::any_register(RegisterClass::SIMD)}},
                    ResultConstraint{AccessTiming::Late,
                                     LocationRequirement::same_as_input(0)});
                invalid.validate(*session.storage());
            },
            "different value representation");
    }

    TEST(JitAllocationConstraints, ValidatesClobberCollisions)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef source(builder.make_instruction<ParameterInstruction>());
        MovInstruction move = builder.make_instruction<MovInstruction>(source);
        RegisterSet x0_clobber;
        x0_clobber.insert(x0);

        // A clobber may follow an early fixed use.
        InstructionAllocationConstraints allowed(
            move, {{0, AccessTiming::Early, fixed(x0)}},
            ResultConstraint{
                AccessTiming::Late,
                LocationRequirement::any_register(RegisterClass::GPR)},
            {}, x0_clobber);
        EXPECT_TRUE(allowed.clobbers().contains(x0));

        InstructionAllocationConstraints operand_copy_allowed(
            move,
            {{0, AccessTiming::Early,
              LocationRequirement::fixed_operand_copy(x0)}},
            ResultConstraint{
                AccessTiming::Late,
                LocationRequirement::any_register(RegisterClass::GPR)},
            {}, x0_clobber);
        EXPECT_TRUE(operand_copy_allowed.clobbers().contains(x0));

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move, {{0, AccessTiming::Early,
                            LocationRequirement::fixed_operand_copy(x0)}});
                invalid.validate(*session.storage());
            },
            "FixedOperandCopy destination must be clobbered");

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move,
                    {{0, AccessTiming::Late,
                      LocationRequirement::fixed_operand_copy(x0)}},
                    std::nullopt, {}, x0_clobber);
                invalid.validate(*session.storage());
            },
            "FixedOperandCopy must be an Early input");

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move, {{0, AccessTiming::Late, fixed(x0)}},
                    ResultConstraint{
                        AccessTiming::Late,
                        LocationRequirement::any_register(RegisterClass::GPR)},
                    {}, x0_clobber);
                invalid.validate(*session.storage());
            },
            "clobber collides with a fixed late input");

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move,
                    {{0, AccessTiming::Early,
                      LocationRequirement::any_register(RegisterClass::GPR)}},
                    ResultConstraint{AccessTiming::Late, fixed(x0)}, {},
                    x0_clobber);
                invalid.validate(*session.storage());
            },
            "clobber collides with a fixed result");

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move,
                    {{0, AccessTiming::Early,
                      LocationRequirement::any_register(RegisterClass::GPR)}},
                    ResultConstraint{
                        AccessTiming::Late,
                        LocationRequirement::any_register(RegisterClass::GPR)},
                    {TemporaryConstraint(fixed(x0))}, x0_clobber);
                invalid.validate(*session.storage());
            },
            "clobber collides with a fixed temporary");

        EXPECT_DEATH(
            {
                InstructionAllocationConstraints invalid(
                    move, {{0, AccessTiming::Early, fixed(x0)}},
                    ResultConstraint{AccessTiming::Late,
                                     LocationRequirement::same_as_input(0)},
                    {}, x0_clobber);
                invalid.validate(*session.storage());
            },
            "clobber collides with a fixed result");
    }

}  // namespace cl::jit
