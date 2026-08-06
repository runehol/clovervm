#include "builtin_types/str.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/instruction.h"
#include "jit/object_pool.h"
#include "jit/side_exit_binding.h"
#include "object_model/validity_cell.h"
#include "object_model/value.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

#include <absl/container/flat_hash_set.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace cl::jit
{
    static_assert(std::ranges::random_access_range<InstructionRange>);

    static_assert(EffectProfile::None < EffectProfile::PythonVisibleEffects);
    static_assert((EffectProfile::SideExit | EffectProfile::Allocate) <
                  EffectProfile::PythonVisibleEffects);
    static_assert(!(EffectProfile::CallPython <
                    EffectProfile::PythonVisibleEffects));
    static_assert(!(EffectProfile::MachineState <
                    EffectProfile::PythonVisibleEffects));
    static_assert(!((EffectProfile::SideExit | EffectProfile::ControlFlow) <
                    EffectProfile::PythonVisibleEffects));

    namespace
    {
        Value test_trusted_unary_handler(ThreadState *, Value value)
        {
            return value;
        }

        TrustedHandlerTarget test_trusted_handler_target()
        {
            return erase_trusted_handler_target(test_trusted_unary_handler);
        }

        class DirectTestObject
        {
        public:
            using Serial = TypedSerial<DirectTestObject>;

            DirectTestObject(Serial serial, int value)
                : serial_(serial), value_(value)
            {
            }

            Serial serial() const { return serial_; }
            int value() const { return value_; }

        private:
            Serial serial_;
            int value_;
        };
    }  // namespace

    TEST(JitObjectPool, AssignsDenseSerialsAndKeepsAddressesStable)
    {
        ObjectPool<DirectTestObject> pool;
        DirectTestObject *first = pool.make(10);
        DirectTestObject *second = pool.make(20);

        for(int value = 0; value < 1024; ++value)
        {
            pool.make(value);
        }

        EXPECT_EQ(0u, first->serial().value());
        EXPECT_EQ(10, first->value());
        EXPECT_EQ(1u, second->serial().value());
        EXPECT_EQ(20, second->value());
    }

    TEST(JitCompilationStorage, UsesOneDenseIdentitySequencePerObjectKind)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *first_block = builder.emplace_block();
        Block *second_block = builder.emplace_block();
        ParameterInstruction first_instruction =
            builder.make_instruction<ParameterInstruction>();
        ParameterInstruction second_instruction =
            builder.make_instruction<ParameterInstruction>();
        BlockEdge *first_edge =
            builder.make_block_edge(first_block, second_block);
        BlockEdge *second_edge =
            builder.make_block_edge(first_block, second_block);
        ConditionalBranchInstruction branch =
            builder.make_instruction<ConditionalBranchInstruction>(
                TaggedValueRef(first_instruction), first_edge, second_edge);

        EXPECT_EQ(0u, first_block->serial().value());
        EXPECT_EQ(1u, second_block->serial().value());
        EXPECT_EQ(0u, first_instruction.id().value());
        EXPECT_EQ(1u, second_instruction.id().value());
        EXPECT_EQ(0u, first_edge->id().value());
        EXPECT_EQ(1u, second_edge->id().value());
        EXPECT_EQ(first_edge, session.storage()->block_edge(first_edge->id()));
        EXPECT_EQ(second_edge,
                  session.storage()->block_edge(second_edge->id()));
        EXPECT_EQ(first_edge, branch.true_edge());
        EXPECT_EQ(second_edge, branch.false_edge());
        EXPECT_EQ(first_edge->id().value(), branch.slot(1));
        EXPECT_EQ(second_edge->id().value(), branch.slot(2));
        static_assert(sizeof(BlockEdgeId) == sizeof(uint32_t));
    }

    TEST(JitCompilationStorage, GraphBorrowsItsOwningStorage)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ConstInstruction none =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(none));

        ControlFlowGraph *graph = builder.finalize();

        EXPECT_EQ(session.storage(), graph->storage());
        EXPECT_EQ(none, entry->instructions().front());
        EXPECT_EQ(none.id(), entry->instruction_ids().front());
    }

    TEST(JitCompilationSession,
         RetainsNewValuesAndPinsGraphConstantsUntilDestruction)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());
        String *created =
            context.thread()->make_internal_raw<String>(L"jit constant");
        TValue<String> created_value = TValue<String>::from_oop(created);
        String *existing =
            context.thread()->make_internal_raw<String>(L"source constant");
        Value existing_value = Value::from_oop(existing);

        EXPECT_EQ(0, created->refcount);
        EXPECT_EQ(0, existing->refcount);
        {
            CompilationSession session;
            GraphBuilder builder(session, IRLevel::Core);
            Block *entry = builder.emplace_block();
            TValue<String> retained_created =
                session.retain_and_pin_value(created_value);
            Value retained_existing =
                builder.retain_and_pin_value(existing_value);
            builder.emplace_instruction<ConstInstruction>(
                entry, retained_created.raw_value());
            ConstInstruction constant =
                builder.emplace_instruction<ConstInstruction>(
                    entry, retained_existing);
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(constant));
            builder.finalize();

            EXPECT_EQ(created_value, retained_created);
            EXPECT_EQ(existing_value, retained_existing);
            EXPECT_EQ(1, created->refcount);
            EXPECT_EQ(1, existing->refcount);
        }
        EXPECT_EQ(0, created->refcount);
        EXPECT_EQ(0, existing->refcount);
    }

    TEST(JitInstructionStorage, HasDenseIdsAndStableHandles)
    {
        static_assert(sizeof(InstructionEntry) == 16);
        static_assert(sizeof(Instruction) == 16);
        static_assert(sizeof(InstructionId) == sizeof(uint32_t));
        static_assert(std::is_trivially_copyable_v<InstructionEntry>);
        static_assert(std::is_trivially_destructible_v<Instruction>);

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        std::vector<Instruction> instructions;

        ParameterInstruction lhs =
            builder.make_instruction<ParameterInstruction>();
        ParameterInstruction rhs =
            builder.make_instruction<ParameterInstruction>();
        SnapshotInstruction snapshot =
            builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>{}, BytecodePCOffset{17});
        AddSMIInstruction add = builder.make_instruction<AddSMIInstruction>(
            TaggedValueRef(lhs), TaggedValueRef(rhs), SnapshotRef(snapshot));
        ConstInstruction constant =
            builder.make_instruction<ConstInstruction>(Value::False());
        TaggedValueRef lhs_reference(lhs);
        SnapshotRef snapshot_reference(snapshot);
        instructions.insert(instructions.end(),
                            {lhs, rhs, snapshot, add, constant});

        for(size_t index = 0; index < 4096; ++index)
        {
            instructions.push_back(
                builder.make_instruction<ParameterInstruction>());
        }

        for(size_t index = 0; index < instructions.size(); ++index)
        {
            Instruction instruction = instructions[index];
            EXPECT_EQ(index, instruction.id().value());
            EXPECT_EQ(instruction,
                      session.storage()->instruction(instruction.id()));
            if(index >= 5)
            {
                EXPECT_EQ(InstructionKind::Parameter, instruction.kind());
            }
        }

        const CompilationStorage *storage = session.storage();
        EXPECT_EQ(instructions.front(),
                  storage->instruction(instructions.front().id()));
        EXPECT_EQ(InstructionKind::Parameter, lhs.kind());
        EXPECT_EQ(Value::False(), constant.constant());
        EXPECT_EQ(lhs.id(), add.lhs().instruction_id());
        EXPECT_EQ(rhs.id(), add.rhs().instruction_id());
        EXPECT_EQ(snapshot.id(), add.snapshot().instruction_id());
        EXPECT_EQ(BytecodePCOffset{17}, snapshot.resume_pc_offset());
        EXPECT_TRUE(snapshot.captured_values().empty());
        EXPECT_EQ(lhs.id(), lhs_reference.instruction_id());
        EXPECT_EQ(snapshot.id(), snapshot_reference.instruction_id());
    }

    TEST(JitInstructionSchema, GeneratesIntrinsicMetadata)
    {
        const InstructionFamilyMetadata &add =
            instruction_kind_metadata(InstructionKind::AddSMI);
        EXPECT_EQ(ResultClass::ProgramValue,
                  instruction_result_class(InstructionKind::AddSMI));
        EXPECT_EQ(ValueRepresentation::TaggedValue,
                  instruction_value_representation(InstructionKind::AddSMI));
        EXPECT_EQ(3u, add.fixed_operand_count);
        EXPECT_EQ(0u, add.attribute_count);
        EXPECT_EQ(3u, add.inline_slot_count);
        EXPECT_FALSE(add.has_variadic_operands);
        EXPECT_FALSE(add.operands_are_indirect);
        EXPECT_EQ(InstructionFamilyMetadata::NoSideExitArguments,
                  add.side_exit_argument_start);

        const InstructionFamilyMetadata &call =
            instruction_kind_metadata(InstructionKind::PythonCall);
        EXPECT_EQ(2u, call.fixed_operand_count);
        EXPECT_EQ(1u, call.attribute_count);
        EXPECT_EQ(Instruction::InlineSlotCount, call.inline_slot_count);
        EXPECT_TRUE(call.has_variadic_operands);
        EXPECT_TRUE(call.operands_are_indirect);
        EXPECT_EQ(InstructionFamilyMetadata::NoSideExitArguments,
                  call.side_exit_argument_start);

        const InstructionFamilyMetadata &side_exit = instruction_kind_metadata(
            InstructionKind::ResumeInInterpreterWithSideExit);
        EXPECT_EQ(ResumeInInterpreterWithSideExitInstruction::
                      side_exit_arguments_operand_index,
                  side_exit.side_exit_argument_start);

        const InstructionFamilyMetadata &inline_guard =
            instruction_kind_metadata(
                InstructionKind::InlineTagGuardWithSideExit);
        EXPECT_EQ(1u, inline_guard.fixed_operand_count);
        EXPECT_EQ(2u, inline_guard.attribute_count);
        EXPECT_EQ(Instruction::InlineSlotCount, inline_guard.inline_slot_count);
        EXPECT_TRUE(inline_guard.has_variadic_operands);
        EXPECT_TRUE(inline_guard.operands_are_indirect);
        EXPECT_EQ(InlineTagGuardWithSideExitInstruction::
                      side_exit_arguments_operand_index,
                  inline_guard.side_exit_argument_start);

        const InstructionFamilyMetadata &guard =
            instruction_kind_metadata(InstructionKind::PointerAndShapeGuard);
        EXPECT_EQ(2u, guard.fixed_operand_count);
        EXPECT_EQ(1u, guard.attribute_count);
        EXPECT_EQ(Instruction::InlineSlotCount, guard.inline_slot_count);
        EXPECT_FALSE(guard.has_variadic_operands);
        EXPECT_FALSE(guard.operands_are_indirect);
    }

    TEST(JitInstructionSchema, GeneratesKindsForEachIRLevel)
    {
        static_assert(
            std::is_same_v<std::underlying_type_t<SemanticInstructionKind>,
                           uint16_t>);
        static_assert(
            std::is_same_v<std::underlying_type_t<CoreInstructionKind>,
                           uint16_t>);
        static_assert(
            std::is_same_v<std::underlying_type_t<MachineInstructionKind>,
                           uint16_t>);
        static_assert(
            std::is_same_v<std::underlying_type_t<TransitionInstructionKind>,
                           uint16_t>);

        EXPECT_EQ(static_cast<uint8_t>(IRLevelMask::Core) |
                      static_cast<uint8_t>(IRLevelMask::Transition),
                  static_cast<uint8_t>(IRLevelMask::CoreTransition));
        EXPECT_EQ(
            static_cast<uint8_t>(IRLevelMask::Semantic) |
                static_cast<uint8_t>(IRLevelMask::Core) |
                static_cast<uint8_t>(IRLevelMask::Machine) |
                static_cast<uint8_t>(IRLevelMask::Transition),
            static_cast<uint8_t>(IRLevelMask::SemanticCoreMachineTransition));
        EXPECT_TRUE(instruction_kind_is_allowed_at(InstructionKind::Return,
                                                   IRLevelMask::Core));
        EXPECT_TRUE(instruction_kind_is_allowed_at(InstructionKind::Return,
                                                   IRLevelMask::Machine));
        EXPECT_FALSE(instruction_kind_is_allowed_at(InstructionKind::Return,
                                                    IRLevelMask::Transition));
        EXPECT_EQ(CoreInstructionKind::Return,
                  core_instruction_kind(InstructionKind::Return));
        EXPECT_EQ(CoreInstructionKind::BareReturn,
                  core_instruction_kind<BareReturnInstruction>());
        EXPECT_EQ(InstructionKind::Return,
                  instruction_kind(CoreInstructionKind::Return));
        EXPECT_EQ(MachineInstructionKind::Return,
                  machine_instruction_kind(InstructionKind::Return));
        EXPECT_EQ(MachineInstructionKind::BareReturn,
                  machine_instruction_kind<BareReturnInstruction>());
        EXPECT_FALSE(instruction_kind_is_allowed_at(InstructionKind::Snapshot,
                                                    IRLevelMask::Machine));
        EXPECT_TRUE(instruction_kind_is_allowed_at(
            InstructionKind::ExitToInterpreter, IRLevelMask::Machine));
        EXPECT_FALSE(instruction_kind_is_allowed_at(
            InstructionKind::ExitToInterpreter, IRLevelMask::Core));
        EXPECT_FALSE(instruction_kind_is_allowed_at(
            InstructionKind::ResumeInInterpreter, IRLevelMask::Machine));
    }

    TEST(JitInstructionSchema, EncodesResultsAndDenseFamilies)
    {
        EXPECT_EQ(ResultClass::None,
                  instruction_result_class(InstructionKind::Return));
        EXPECT_EQ(ValueRepresentation::None,
                  instruction_value_representation(InstructionKind::Return));
        EXPECT_EQ(ResultClass::Snapshot,
                  instruction_result_class(InstructionKind::Snapshot));
        EXPECT_EQ(ValueRepresentation::None,
                  instruction_value_representation(InstructionKind::Snapshot));
        EXPECT_EQ(ResultClass::None,
                  instruction_result_class(InstructionKind::ExitToInterpreter));
        EXPECT_EQ(ValueRepresentation::None,
                  instruction_value_representation(
                      InstructionKind::ExitToInterpreter));
        EXPECT_EQ(ResultClass::ProgramValue,
                  instruction_result_class(InstructionKind::ParameterF64));
        EXPECT_EQ(ValueRepresentation::F64, instruction_value_representation(
                                                InstructionKind::ParameterF64));

        EXPECT_EQ(InstructionFamilyKind::Parameter,
                  instruction_family_kind(InstructionKind::Parameter));
        EXPECT_EQ(InstructionFamilyKind::ParameterF64,
                  instruction_family_kind(InstructionKind::ParameterF64));
        EXPECT_EQ(InstructionFamilyKind::IsComparison,
                  instruction_family_kind(InstructionKind::Is));
        EXPECT_EQ(InstructionFamilyKind::IsComparison,
                  instruction_family_kind(InstructionKind::IsNot));
        EXPECT_EQ(InstructionFamilyKind::BinaryLogicalSMI,
                  instruction_family_kind(InstructionKind::AndSMI));
        EXPECT_EQ(InstructionFamilyKind::BinaryLogicalSMI,
                  instruction_family_kind(InstructionKind::OrrSMI));
        EXPECT_EQ(InstructionFamilyKind::BinaryLogicalSMI,
                  instruction_family_kind(InstructionKind::EorSMI));
        EXPECT_EQ(InstructionFamilyKind::BinaryArithmeticSMIWithSnapshot,
                  instruction_family_kind(InstructionKind::AddSMI));
        EXPECT_EQ(InstructionFamilyKind::BinaryArithmeticSMIWithSideExit,
                  instruction_family_kind(InstructionKind::AddSMIWithSideExit));
        EXPECT_EQ(0u, instruction_subkind(InstructionKind::Parameter));
        EXPECT_EQ(0u, instruction_subkind(InstructionKind::ParameterF64));
        EXPECT_EQ(static_cast<uint16_t>(InstructionKind::Is),
                  static_cast<uint16_t>(IsComparisonSubkind::Is));
        EXPECT_EQ(static_cast<uint16_t>(InstructionKind::IsNot),
                  static_cast<uint16_t>(IsComparisonSubkind::IsNot));
        EXPECT_EQ(static_cast<uint16_t>(InstructionKind::EorSMI),
                  static_cast<uint16_t>(BinaryLogicalSMISubkind::EorSMI));
        EXPECT_EQ(
            &instruction_family_metadata(InstructionFamilyKind::Parameter),
            &instruction_kind_metadata(InstructionKind::Parameter));
        EXPECT_EQ(&instruction_kind_metadata(InstructionKind::Parameter) + 1,
                  &instruction_kind_metadata(InstructionKind::ParameterF64));
        EXPECT_EQ(&instruction_kind_metadata(InstructionKind::Is),
                  &instruction_kind_metadata(InstructionKind::IsNot));
    }

    TEST(JitInstructionSchema, ConstructsConcreteMembersThroughFamilySchema)
    {
        static_assert(
            std::is_base_of_v<IsComparisonInstruction, IsInstruction>);
        static_assert(
            std::is_base_of_v<IsComparisonInstruction, IsNotInstruction>);
        static_assert(
            std::is_base_of_v<BinaryLogicalSMIInstruction, AndSMIInstruction>);
        static_assert(
            std::is_base_of_v<BinaryArithmeticSMIWithSnapshotInstruction,
                              AddSMIInstruction>);
        static_assert(
            std::is_base_of_v<BinaryArithmeticSMIWithSideExitInstruction,
                              AddSMIWithSideExitInstruction>);
        static_assert(sizeof(IsComparisonInstruction) == sizeof(Instruction));

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        ParameterInstruction lhs =
            builder.make_instruction<ParameterInstruction>();
        ParameterInstruction rhs =
            builder.make_instruction<ParameterInstruction>();
        IsInstruction is = builder.make_instruction<IsInstruction>(
            TaggedValueRef(lhs), TaggedValueRef(rhs));
        IsNotInstruction is_not = builder.make_instruction<IsNotInstruction>(
            TaggedValueRef(lhs), TaggedValueRef(rhs));

        EXPECT_EQ(IsComparisonSubkind::Is,
                  Instruction(is).as<IsComparisonInstruction>().subkind());
        EXPECT_EQ(IsComparisonSubkind::IsNot,
                  Instruction(is_not).as<IsComparisonInstruction>().subkind());
        EXPECT_EQ(lhs.id(), is.lhs().instruction_id());
        EXPECT_EQ(rhs.id(), is_not.rhs().instruction_id());
    }

    TEST(JitInstructionSchema, GeneratesConcreteTypedInstructionClasses)
    {
        static_assert(std::is_base_of_v<Instruction, AddSMIInstruction>);
        static_assert(sizeof(AddSMIInstruction) == sizeof(Instruction));
        static_assert(std::is_same_v<
                      decltype(std::declval<GraphBuilder &>()
                                   .make_instruction<ParameterInstruction>()),
                      ParameterInstruction>);
        static_assert(std::is_same_v<
                      decltype(std::declval<const AddSMIInstruction &>().lhs()),
                      TaggedValueRef>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const AddSMIInstruction &>().snapshot()),
                SnapshotRef>);
        static_assert(std::is_same_v<
                      decltype(std::declval<const AddF64Instruction &>().lhs()),
                      F64Ref>);
        static_assert(std::is_same_v<
                      decltype(std::declval<const ShapeGuardInstruction &>()
                                   .expected_shape()),
                      Shape *>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const ValidityCellGuardInstruction &>()
                             .value()),
                TaggedValueRef>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const ValidityCellGuardInstruction &>()
                             .validity()),
                ValidityCell *>);
        static_assert(std::is_same_v<
                      decltype(std::declval<const InlineTagGuardInstruction &>()
                                   .expected_class()),
                      TaggedValueClass>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const PythonCallInstruction &>()
                             .arguments()),
                RepresentedValueRefRange<ValueRepresentation::TaggedValue>>);
        static_assert(
            std::is_same_v<decltype(std::declval<const SnapshotInstruction &>()
                                        .captured_values()),
                           ProgramValueRefRange>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const ExitToInterpreterInstruction &>()
                             .captured_values()),
                ProgramValueRefRange>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const ConditionalBranchInstruction &>()
                             .true_edge()),
                BlockEdge *>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const ResumeInInterpreterInstruction &>()
                             .snapshot()),
                SnapshotRef>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<
                             const InlineTagGuardWithSideExitInstruction &>()
                             .side_exit_arguments()),
                ProgramValueRefRange>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<
                             const ResumeInInterpreterWithSideExitInstruction
                                 &>()
                             .side_exit_arguments()),
                ProgramValueRefRange>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<
                             const ResumeInInterpreterWithSideExitInstruction
                                 &>()
                             .side_exit_region()),
                SideExitRegionId>);
        EXPECT_EQ(InstructionKind::AddSMI, AddSMIInstruction::Kind);
        EXPECT_EQ(InstructionKind::Snapshot, SnapshotInstruction::Kind);
        EXPECT_EQ(InstructionKind::ExitToInterpreter,
                  ExitToInterpreterInstruction::Kind);
        EXPECT_EQ(InstructionKind::Uninitialized,
                  UninitializedInstruction::Kind);
        EXPECT_EQ(InstructionKind::ResumeInInterpreter,
                  ResumeInInterpreterInstruction::Kind);
        EXPECT_EQ(InstructionKind::ResumeInInterpreterWithSideExit,
                  ResumeInInterpreterWithSideExitInstruction::Kind);
        EXPECT_EQ(InstructionKind::InlineTagGuardWithSideExit,
                  InlineTagGuardWithSideExitInstruction::Kind);
        EXPECT_EQ(InstructionKind::ConditionalBranch,
                  ConditionalBranchInstruction::Kind);
        EXPECT_EQ(ResultClass::ProgramValue, AddSMIInstruction::Result);
        EXPECT_EQ(ValueRepresentation::TaggedValue,
                  AddSMIInstruction::Representation);
        EXPECT_EQ(EffectProfile::None, AddSMIInstruction::MustEffects);
        EXPECT_EQ(EffectProfile::SideExit, AddSMIInstruction::MayEffects);
        EXPECT_EQ(EffectProfile::None, UnboxF64Instruction::MustEffects);
        EXPECT_EQ(EffectProfile::None, UnboxF64Instruction::MayEffects);
        EXPECT_EQ(IRLevelMask::Core, AddSMIInstruction::AllowedIRLevels);
        EXPECT_FALSE(AddSMIInstruction::IsVariadic);
        EXPECT_FALSE(AddSMIInstruction::OperandsAreIndirect);
        EXPECT_TRUE(PythonCallInstruction::IsVariadic);
        EXPECT_TRUE(PythonCallInstruction::OperandsAreIndirect);
        EXPECT_TRUE(SnapshotInstruction::IsVariadic);
        EXPECT_TRUE(SnapshotInstruction::OperandsAreIndirect);
        EXPECT_TRUE(ExitToInterpreterInstruction::IsVariadic);
        EXPECT_TRUE(ExitToInterpreterInstruction::OperandsAreIndirect);
        EXPECT_FALSE(ShapeGuardInstruction::OperandsAreIndirect);
        EXPECT_FALSE(ValidityCellGuardInstruction::OperandsAreIndirect);
        EXPECT_FALSE(InlineTagGuardInstruction::OperandsAreIndirect);
        EXPECT_EQ(1u, instruction_kind_metadata(InstructionKind::UnboxF64)
                          .fixed_operand_count);
        EXPECT_FALSE(UnboxF64Instruction::OperandsAreIndirect);
        EXPECT_FALSE(ConstInstruction::OperandsAreIndirect);
        EXPECT_EQ(ResultClass::Snapshot, SnapshotInstruction::Result);
        EXPECT_EQ(ValueRepresentation::None,
                  SnapshotInstruction::Representation);
        EXPECT_EQ(ResultClass::ProgramValue, UninitializedInstruction::Result);
        EXPECT_EQ(ValueRepresentation::TaggedValue,
                  UninitializedInstruction::Representation);
        constexpr EffectProfile side_exit_control_flow =
            EffectProfile::SideExit | EffectProfile::ControlFlow;
        EXPECT_EQ(side_exit_control_flow,
                  CheckNotImplementedInstruction::MayEffects);
        constexpr EffectProfile terminating_side_exit =
            side_exit_control_flow | EffectProfile::TerminateBlock;
        EXPECT_EQ(terminating_side_exit,
                  ResumeInInterpreterInstruction::MustEffects);
        EXPECT_EQ(terminating_side_exit,
                  ResumeInInterpreterInstruction::MayEffects);
        EXPECT_TRUE(has_effects(ResumeInInterpreterInstruction::MustEffects,
                                EffectProfile::TerminateBlock));
        EXPECT_EQ(IRLevelMask::Machine,
                  ResumeInInterpreterWithSideExitInstruction::AllowedIRLevels);
        EXPECT_EQ(IRLevelMask::Machine,
                  InlineTagGuardWithSideExitInstruction::AllowedIRLevels);
        EXPECT_EQ(terminating_side_exit,
                  ResumeInInterpreterWithSideExitInstruction::MustEffects);
        EXPECT_EQ(terminating_side_exit,
                  ExitToInterpreterInstruction::MustEffects);
        EXPECT_TRUE(
            ResumeInInterpreterWithSideExitInstruction::OperandsAreIndirect);
        constexpr EffectProfile terminating_control_flow =
            EffectProfile::ControlFlow | EffectProfile::TerminateBlock;
        EXPECT_EQ(terminating_control_flow, BareReturnInstruction::MustEffects);
        EXPECT_EQ(terminating_control_flow, BareReturnInstruction::MayEffects);
        EXPECT_EQ(EffectProfile::MachineState,
                  SaveLinkRegisterToFrameInstruction::MustEffects);
        EXPECT_EQ(EffectProfile::MachineState,
                  SaveLinkRegisterToFrameInstruction::MayEffects);
        EXPECT_EQ(EffectProfile::MachineState,
                  RestoreLinkRegisterFromFrameInstruction::MustEffects);
        EXPECT_EQ(EffectProfile::MachineState,
                  RestoreLinkRegisterFromFrameInstruction::MayEffects);
    }

    TEST(JitInstructionConstruction, EncodesFixedAttributes)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        ConstInstruction instruction =
            builder.make_instruction<ConstInstruction>(Value::False());

        EXPECT_EQ(InstructionKind::Const, instruction.kind());
        EXPECT_EQ(Value::False(), instruction.constant());
        EXPECT_EQ(0u, instruction.operand_count());
        EXPECT_FALSE(instruction.operands_are_indirect());
        EXPECT_EQ(instruction, instruction.as<ConstInstruction>());

        UninitializedInstruction uninitialized =
            builder.make_instruction<UninitializedInstruction>();
        EXPECT_EQ(InstructionKind::Uninitialized, uninitialized.kind());
        EXPECT_EQ(0u, uninitialized.operand_count());
        EXPECT_FALSE(uninitialized.operands_are_indirect());
    }

    TEST(JitInstructionConstruction, ResolvesPooledAttributesAfterStorageGrowth)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());
        Shape *shape = context.vm().str_instance_root_shape();
        ValidityCell *validity =
            context.thread()->make_internal_raw<ValidityCell>();

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef value(builder.make_instruction<ParameterInstruction>());
        ConstInstruction constant =
            builder.make_instruction<ConstInstruction>(Value::True());
        SnapshotRef snapshot(builder.make_instruction<SnapshotInstruction>(
            std::span<const ProgramValueRef>{}, BytecodePCOffset{17}));
        ShapeGuardInstruction shape_guard =
            builder.make_instruction<ShapeGuardInstruction>(
                ShapeGuardSubkind::PointerAndShapeGuard, value, snapshot,
                shape);
        ValidityCellGuardInstruction validity_guard =
            builder.make_instruction<ValidityCellGuardInstruction>(
                value, snapshot, validity);
        std::array<TaggedValueRef, 1> handler_arguments = {value};
        TrustedHandlerCallInstruction handler_call =
            builder.make_instruction<TrustedHandlerCallInstruction>(
                std::span<const TaggedValueRef>(handler_arguments),
                test_trusted_handler_target());

        std::array<ProgramValueRef, 1> captured = {value};
        for(size_t index = 0; index < 1024; ++index)
        {
            builder.make_instruction<ConstInstruction>(
                Value::from_smi(static_cast<int64_t>(index)));
            builder.make_instruction<ShapeGuardInstruction>(
                ShapeGuardSubkind::PointerAndShapeGuard, value, snapshot,
                shape);
            builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>(captured),
                BytecodePCOffset{17});
            builder.make_instruction<TrustedHandlerCallInstruction>(
                std::span<const TaggedValueRef>(handler_arguments),
                test_trusted_handler_target());
        }

        EXPECT_EQ(Value::True(), constant.constant());
        EXPECT_EQ(shape, shape_guard.expected_shape());
        EXPECT_EQ(validity, validity_guard.validity());
        EXPECT_EQ(test_trusted_handler_target(), handler_call.handler());
        ASSERT_EQ(1u, handler_call.arguments().size());
        EXPECT_EQ(value.instruction_id(),
                  handler_call.arguments()[0].instruction_id());
        EXPECT_EQ(EffectProfile::MachineState,
                  TrustedHandlerCallInstruction::MustEffects);
        EXPECT_EQ(EffectProfile::MachineState | EffectProfile::CallPython,
                  TrustedHandlerCallInstruction::MayEffects);

        for(Instruction instruction:
            {Instruction(shape_guard), Instruction(validity_guard)})
        {
            ASSERT_FALSE(instruction.operands_are_indirect());
            ASSERT_EQ(2u, instruction.operand_count());
            EXPECT_EQ(value.instruction_id().value(),
                      instruction.operand_word(0));
            EXPECT_EQ(snapshot.instruction_id().value(),
                      instruction.operand_word(1));
        }

        InlineTagGuardInstruction inline_guard =
            builder.make_instruction<InlineTagGuardInstruction>(
                value, snapshot, TaggedValueClass::smi_or_boolean());
        EXPECT_EQ(TaggedValueClass::smi_or_boolean(),
                  inline_guard.expected_class());
        EXPECT_FALSE(inline_guard.operands_are_indirect());
        EXPECT_EQ(value.instruction_id().value(), inline_guard.operand_word(0));
        EXPECT_EQ(snapshot.instruction_id().value(),
                  inline_guard.operand_word(1));
    }

    TEST(JitInstructionTraversal, WalksProgramValueAndSnapshotReferences)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef lhs(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef rhs(
            builder.make_instruction<ConstInstruction>(Value::from_smi(3)));
        SnapshotRef snapshot(builder.make_instruction<SnapshotInstruction>(
            std::span<const ProgramValueRef>{}, BytecodePCOffset{17}));
        AddSMIInstruction add =
            builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);

        EXPECT_EQ(3u, add.operand_count());
        EXPECT_FALSE(add.operands_are_indirect());
        EXPECT_EQ(lhs.instruction_id().value(), add.slot(0));
        EXPECT_EQ(rhs.instruction_id().value(), add.slot(1));
        EXPECT_EQ(snapshot.instruction_id().value(), add.slot(2));
        EXPECT_EQ(lhs.instruction_id(), add.lhs().instruction_id());
        EXPECT_EQ(rhs.instruction_id(), add.rhs().instruction_id());
        EXPECT_EQ(snapshot.instruction_id(), add.snapshot().instruction_id());

        std::vector<std::pair<OperandClass, InstructionId>> references;
        visit_operand_references(
            add, [&](uint32_t operand_index, OperandClass operand_class,
                     ValueRepresentationRequirement representation,
                     InstructionId def) {
                EXPECT_EQ(references.size(), operand_index);
                EXPECT_EQ(operand_class == OperandClass::ProgramValue
                              ? ValueRepresentationRequirement::TaggedValue
                              : ValueRepresentationRequirement::Any,
                          representation);
                references.emplace_back(operand_class, def);
            });

        ASSERT_EQ(3u, references.size());
        EXPECT_EQ(OperandClass::ProgramValue, references[0].first);
        EXPECT_EQ(lhs.instruction_id(), references[0].second);
        EXPECT_EQ(rhs.instruction_id(), references[1].second);
        EXPECT_EQ(OperandClass::Snapshot, references[2].first);
        EXPECT_EQ(snapshot.instruction_id(), references[2].second);
    }

    TEST(JitInstructionTraversal, WalksVariadicPythonCallArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef callable(
            builder.make_instruction<ParameterInstruction>());
        TaggedValueRef first(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef second(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef none(
            builder.make_instruction<ConstInstruction>(Value::None()));
        SnapshotRef snapshot(builder.make_instruction<SnapshotInstruction>(
            std::span<const ProgramValueRef>{}, BytecodePCOffset{23}));
        std::array<TaggedValueRef, 3> arguments = {first, none, second};
        PythonCallInstruction call =
            builder.make_instruction<PythonCallInstruction>(
                callable, snapshot, std::span<const TaggedValueRef>(arguments),
                BytecodePCOffset{23});
        auto retained_arguments = call.arguments();
        PythonCallInstruction call_without_arguments =
            builder.make_instruction<PythonCallInstruction>(
                callable, snapshot, std::span<const TaggedValueRef>{},
                BytecodePCOffset{41});
        std::array<ProgramValueRef, 1> captured = {first};
        for(size_t index = 0; index < 1024; ++index)
        {
            builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>(captured),
                BytecodePCOffset{51});
        }

        EXPECT_EQ(5u, call.operand_count());
        EXPECT_TRUE(call.operands_are_indirect());
        EXPECT_EQ(23u, call.slot(0));
        EXPECT_EQ(callable.instruction_id().value(), call.operand_word(0));
        EXPECT_EQ(snapshot.instruction_id().value(), call.operand_word(1));
        EXPECT_EQ(2u, call_without_arguments.operand_count());
        EXPECT_TRUE(call_without_arguments.operands_are_indirect());
        EXPECT_EQ(41u, call_without_arguments.slot(0));
        EXPECT_EQ(3u, call.arguments().size());
        EXPECT_EQ(first.instruction_id(), call.arguments()[0].instruction_id());
        EXPECT_EQ(none.instruction_id(), call.arguments()[1].instruction_id());
        EXPECT_EQ(second.instruction_id(),
                  call.arguments()[2].instruction_id());
        EXPECT_EQ(first.instruction_id(),
                  retained_arguments[0].instruction_id());
        EXPECT_EQ(none.instruction_id(),
                  retained_arguments[1].instruction_id());
        EXPECT_EQ(second.instruction_id(),
                  retained_arguments[2].instruction_id());
        EXPECT_EQ(23u, call.interpreter_return_pc_offset());
        Instruction snapshot_instruction =
            builder.storage()->instruction(snapshot.instruction_id());
        EXPECT_EQ(0u, snapshot_instruction.operand_count());
        EXPECT_TRUE(snapshot_instruction.operands_are_indirect());
        EXPECT_EQ(23u, snapshot_instruction.slot(0));
        EXPECT_EQ(0u,
                  snapshot_instruction.slot(Instruction::IndirectOperandSlot));

        std::vector<std::pair<OperandClass, InstructionId>> references;
        visit_operand_references(
            call, [&](uint32_t operand_index, OperandClass operand_class,
                      ValueRepresentationRequirement representation,
                      InstructionId def) {
                EXPECT_EQ(references.size(), operand_index);
                EXPECT_EQ(operand_class == OperandClass::ProgramValue
                              ? ValueRepresentationRequirement::TaggedValue
                              : ValueRepresentationRequirement::Any,
                          representation);
                references.emplace_back(operand_class, def);
            });

        ASSERT_EQ(5u, references.size());
        EXPECT_EQ(callable.instruction_id(), references[0].second);
        EXPECT_EQ(OperandClass::Snapshot, references[1].first);
        EXPECT_EQ(snapshot.instruction_id(), references[1].second);
        EXPECT_EQ(first.instruction_id(), references[2].second);
        EXPECT_EQ(none.instruction_id(), references[3].second);
        EXPECT_EQ(second.instruction_id(), references[4].second);
    }

    TEST(JitInstructionTraversal, SnapshotStoresProgramValueReferences)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        TaggedValueRef tagged(builder.make_instruction<ParameterInstruction>());
        F64Ref f64(builder.make_instruction<ParameterF64Instruction>());
        TaggedValueRef truth(
            builder.make_instruction<ConstInstruction>(Value::True()));
        TaggedValueRef none(
            builder.make_instruction<ConstInstruction>(Value::None()));
        std::array<ProgramValueRef, 4> captured_values = {tagged, f64, truth,
                                                          none};

        EXPECT_EQ(4u, SnapshotInstruction::n_indirect_slots_for(
                          std::span<const ProgramValueRef>(captured_values),
                          BytecodePCOffset{91}));

        SnapshotInstruction snapshot =
            builder.make_instruction<SnapshotInstruction>(
                std::span<const ProgramValueRef>(captured_values),
                BytecodePCOffset{91});

        ASSERT_EQ(4u, snapshot.operand_count());
        ASSERT_TRUE(snapshot.operands_are_indirect());
        EXPECT_EQ(tagged.instruction_id().value(), snapshot.operand_word(0));
        EXPECT_EQ(f64.instruction_id().value(), snapshot.operand_word(1));
        EXPECT_EQ(truth.instruction_id().value(), snapshot.operand_word(2));
        EXPECT_EQ(none.instruction_id().value(), snapshot.operand_word(3));

        ProgramValueRefRange values = snapshot.captured_values();
        ASSERT_EQ(4u, values.size());
        EXPECT_EQ(tagged.instruction_id(), values[0].instruction_id());
        EXPECT_EQ(f64.instruction_id(), values[1].instruction_id());
        EXPECT_EQ(truth.instruction_id(), values[2].instruction_id());
        EXPECT_EQ(none.instruction_id(), values[3].instruction_id());
        EXPECT_EQ(SnapshotRef(snapshot), SnapshotRef(snapshot));
        EXPECT_EQ(TaggedValueRef(tagged), TaggedValueRef(tagged));
        ParameterPointerInstruction pointer_parameter =
            builder.make_instruction<ParameterPointerInstruction>();
        PointerRef pointer(pointer_parameter);
        MovPointerInstruction pointer_move =
            builder.make_instruction<MovPointerInstruction>(pointer);
        EXPECT_EQ(ValueRepresentation::Pointer,
                  pointer_move.value_representation());
        EXPECT_EQ(pointer.instruction_id(),
                  pointer_move.source().instruction_id());
        EXPECT_EQ(pointer, PointerRef(pointer_parameter));
        EXPECT_EQ(F64Ref(f64), F64Ref(f64));
        EXPECT_EQ(91u, snapshot.resume_pc_offset());

        std::vector<InstructionId> references;
        visit_operand_references(
            snapshot, [&](uint32_t operand_index, OperandClass operand_class,
                          ValueRepresentationRequirement representation,
                          InstructionId def) {
                EXPECT_EQ(references.size(), operand_index);
                EXPECT_EQ(OperandClass::ProgramValue, operand_class);
                EXPECT_EQ(ValueRepresentationRequirement::Any, representation);
                references.push_back(def);
            });
        ASSERT_EQ(4u, references.size());
        EXPECT_EQ(tagged.instruction_id(), references[0]);
        EXPECT_EQ(f64.instruction_id(), references[1]);
        EXPECT_EQ(truth.instruction_id(), references[2]);
        EXPECT_EQ(none.instruction_id(), references[3]);
    }

    TEST(JitSideExitBinding, ComparesAndHashesByRegionAndOrderedArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        ParameterInstruction first =
            builder.make_instruction<ParameterInstruction>();
        ParameterInstruction second =
            builder.make_instruction<ParameterInstruction>();
        std::array<ProgramValueRef, 2> arguments = {ProgramValueRef(first),
                                                    ProgramValueRef(second)};
        ResumeInInterpreterWithSideExitInstruction owner =
            builder
                .make_instruction<ResumeInInterpreterWithSideExitInstruction>(
                    arguments, SideExitRegionId{7});
        std::array<ProgramValueRef, 2> reversed = {ProgramValueRef(second),
                                                   ProgramValueRef(first)};
        ResumeInInterpreterWithSideExitInstruction other_owner =
            builder
                .make_instruction<ResumeInInterpreterWithSideExitInstruction>(
                    reversed, SideExitRegionId{7});

        SideExitBinding binding = make_side_exit_binding(owner);
        SideExitBinding same = make_side_exit_binding(owner);
        SideExitBinding different_order = make_side_exit_binding(other_owner);
        SideExitBinding different_region{SideExitRegionId{8},
                                         owner.side_exit_arguments()};

        EXPECT_EQ(binding, same);
        EXPECT_NE(binding, different_order);
        EXPECT_NE(binding, different_region);

        absl::flat_hash_set<SideExitBinding> bindings;
        EXPECT_TRUE(bindings.insert(binding).second);
        EXPECT_FALSE(bindings.insert(same).second);
        EXPECT_TRUE(bindings.insert(different_order).second);
        EXPECT_TRUE(bindings.insert(different_region).second);
        EXPECT_EQ(3u, bindings.size());
    }

}  // namespace cl::jit
