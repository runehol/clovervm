#include "jit/compilation_arena.h"
#include "jit/graph_builder.h"
#include "jit/instruction.h"
#include "jit/object_pool.h"
#include "object_model/value.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
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

    TEST(JitCompilationArena, UsesOneSerialSequencePerPool)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *first_block = builder.make_block();
        Block *second_block = builder.make_block();
        ParameterInstruction *first_instruction =
            builder.make_instruction<ParameterInstruction>();
        ParameterInstruction *second_instruction =
            builder.make_instruction<ParameterInstruction>();

        EXPECT_EQ(0u, first_block->serial().value());
        EXPECT_EQ(1u, second_block->serial().value());
        EXPECT_EQ(0u, first_instruction->serial().value());
        EXPECT_EQ(1u, second_instruction->serial().value());
    }

    TEST(JitInstructionStorage, HasFiveSlotsAndAlignedStableAddresses)
    {
        static_assert(sizeof(Instruction) == 48);
        static_assert(std::is_trivially_destructible_v<Instruction>);

        CompilationArena arena;
        GraphBuilder builder(arena);
        std::vector<Instruction *> instructions;
        for(size_t index = 0; index < 256; ++index)
        {
            instructions.push_back(
                builder.make_instruction<ParameterInstruction>());
        }

        for(size_t index = 0; index < instructions.size(); ++index)
        {
            Instruction *instruction = instructions[index];
            EXPECT_EQ(index, instruction->serial().value());
            EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(instruction) %
                              alignof(Instruction));
            EXPECT_EQ(InstructionKind::Parameter, instruction->kind());
        }
    }

    TEST(JitInstructionSchema, GeneratesIntrinsicMetadata)
    {
        const InstructionKindMetadata &add =
            instruction_kind_metadata(InstructionKind::AddSMI);
        EXPECT_EQ(ResultClass::ProgramValue,
                  instruction_result_class(InstructionKind::AddSMI));
        EXPECT_EQ(ValueRepresentation::TaggedValue,
                  instruction_value_representation(InstructionKind::AddSMI));
        EXPECT_EQ(3u, add.fixed_operand_count);
        EXPECT_EQ(0u, add.attribute_count);
        EXPECT_EQ(3u, add.inline_slot_count);
        EXPECT_FALSE(add.has_variadic_operands);

        const InstructionKindMetadata &call =
            instruction_kind_metadata(InstructionKind::PythonCall);
        EXPECT_EQ(2u, call.fixed_operand_count);
        EXPECT_EQ(1u, call.attribute_count);
        EXPECT_EQ(2u, call.inline_slot_count);
        EXPECT_TRUE(call.has_variadic_operands);
    }

    TEST(JitInstructionSchema, EncodesResultsWhileKeepingDenseOrdinals)
    {
        EXPECT_EQ(ResultClass::None,
                  instruction_result_class(InstructionKind::Return));
        EXPECT_EQ(ValueRepresentation::None,
                  instruction_value_representation(InstructionKind::Return));
        EXPECT_EQ(ResultClass::Snapshot,
                  instruction_result_class(InstructionKind::Snapshot));
        EXPECT_EQ(ValueRepresentation::None,
                  instruction_value_representation(InstructionKind::Snapshot));
        EXPECT_EQ(ResultClass::ProgramValue,
                  instruction_result_class(InstructionKind::ParameterF64));
        EXPECT_EQ(ValueRepresentation::F64, instruction_value_representation(
                                                InstructionKind::ParameterF64));

        EXPECT_EQ(0u, static_cast<uint16_t>(
                          instruction_ordinal(InstructionKind::Parameter)));
        EXPECT_EQ(1u, static_cast<uint16_t>(
                          instruction_ordinal(InstructionKind::ParameterF64)));
        EXPECT_EQ(&instruction_kind_metadata(InstructionKind::Parameter) + 1,
                  &instruction_kind_metadata(InstructionKind::ParameterF64));
    }

    TEST(JitInstructionSchema, GeneratesConcreteTypedInstructionClasses)
    {
        static_assert(std::is_base_of_v<Instruction, AddSMIInstruction>);
        static_assert(sizeof(AddSMIInstruction) == sizeof(Instruction));
        static_assert(std::is_same_v<
                      decltype(std::declval<GraphBuilder &>()
                                   .make_instruction<ParameterInstruction>()),
                      ParameterInstruction *>);
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
        static_assert(std::is_same_v<
                      decltype(std::declval<const PythonCallInstruction &>()
                                   .arguments()),
                      ProgramValueRefRange<ValueRepresentation::TaggedValue>>);
        static_assert(
            std::is_same_v<decltype(std::declval<const SnapshotInstruction &>()
                                        .captured_values()),
                           SnapshotValuesView>);
        static_assert(
            std::is_same_v<
                decltype(std::declval<const ConditionalBranchInstruction &>()
                             .true_edge()),
                BlockEdge *>);

        EXPECT_EQ(InstructionKind::AddSMI, AddSMIInstruction::Kind);
        EXPECT_EQ(InstructionKind::Snapshot, SnapshotInstruction::Kind);
        EXPECT_EQ(ResultClass::ProgramValue, AddSMIInstruction::Result);
        EXPECT_EQ(ValueRepresentation::TaggedValue,
                  AddSMIInstruction::Representation);
        EXPECT_EQ(EffectProfile::None, AddSMIInstruction::MustEffects);
        EXPECT_EQ(EffectProfile::Deoptimize, AddSMIInstruction::MayEffects);
        EXPECT_EQ(IRLevelMask::Core, AddSMIInstruction::AllowedIRLevels);
        EXPECT_FALSE(AddSMIInstruction::IsVariadic);
        EXPECT_TRUE(PythonCallInstruction::IsVariadic);
        EXPECT_TRUE(SnapshotInstruction::IsVariadic);
        EXPECT_EQ(ResultClass::Snapshot, SnapshotInstruction::Result);
        EXPECT_EQ(ValueRepresentation::None,
                  SnapshotInstruction::Representation);
        EXPECT_EQ(EffectProfile::TerminateBlock,
                  ReturnInstruction::MustEffects);
        EXPECT_EQ(EffectProfile::TerminateBlock, ReturnInstruction::MayEffects);
    }

    TEST(JitInstructionConstruction, EncodesFixedAttributes)
    {
        static_assert(
            !std::is_constructible_v<TaggedValueRef, InlineValueConstant>);

        CompilationArena arena;
        GraphBuilder builder(arena);
        InlineValueConstant constant(Value::False());
        ConstInstruction *instruction =
            builder.make_instruction<ConstInstruction>(constant);

        EXPECT_EQ(InstructionKind::Const, instruction->kind());
        EXPECT_EQ(Value::False(), instruction->constant().value());
        EXPECT_EQ(0u, instruction->operand_count());
        EXPECT_FALSE(instruction->operands_are_indirect());
        EXPECT_EQ(instruction, instruction->as<ConstInstruction>());

        LoadConstInstruction *load =
            builder.make_instruction<LoadConstInstruction>(Value::None());
        EXPECT_EQ(InstructionKind::LoadConst, load->kind());
        EXPECT_EQ(Value::None(), load->constant());
        EXPECT_EQ(0u, load->operand_count());
    }

    TEST(JitInstructionTraversal, WalksProgramValueAndSnapshotReferences)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        TaggedValueRef lhs(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef rhs(builder.make_instruction<ConstInstruction>(
            InlineValueConstant(Value::from_smi(3))));
        SnapshotRef snapshot(builder.make_instruction<SnapshotInstruction>(
            std::span<const SnapshotValue>{}, BytecodePC{17}));
        AddSMIInstruction *add =
            builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);

        EXPECT_EQ(3u, add->operand_count());
        EXPECT_FALSE(add->operands_are_indirect());
        EXPECT_EQ(lhs.instruction(), add->lhs().instruction());
        EXPECT_EQ(rhs.instruction(), add->rhs().instruction());
        EXPECT_EQ(snapshot.instruction(), add->snapshot().instruction());

        std::vector<std::pair<OperandClass, Instruction *>> references;
        visit_operand_references(*add, [&](OperandClass operand_class,
                                           ValueRepresentation representation,
                                           Instruction *producer) {
            EXPECT_EQ(operand_class == OperandClass::ProgramValue
                          ? ValueRepresentation::TaggedValue
                          : ValueRepresentation::None,
                      representation);
            references.emplace_back(operand_class, producer);
        });

        ASSERT_EQ(3u, references.size());
        EXPECT_EQ(OperandClass::ProgramValue, references[0].first);
        EXPECT_EQ(lhs.instruction(), references[0].second);
        EXPECT_EQ(rhs.instruction(), references[1].second);
        EXPECT_EQ(OperandClass::Snapshot, references[2].first);
        EXPECT_EQ(snapshot.instruction(), references[2].second);
    }

    TEST(JitInstructionTraversal, WalksVariadicPythonCallArguments)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        TaggedValueRef callable(
            builder.make_instruction<ParameterInstruction>());
        TaggedValueRef first(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef second(builder.make_instruction<ParameterInstruction>());
        TaggedValueRef none(builder.make_instruction<ConstInstruction>(
            InlineValueConstant(Value::None())));
        SnapshotRef snapshot(builder.make_instruction<SnapshotInstruction>(
            std::span<const SnapshotValue>{}, BytecodePC{23}));
        std::array<TaggedValueRef, 3> arguments = {first, none, second};
        PythonCallInstruction *call =
            builder.make_instruction<PythonCallInstruction>(
                callable, snapshot, std::span<const TaggedValueRef>(arguments),
                BytecodePC{23});
        PythonCallInstruction *call_without_arguments =
            builder.make_instruction<PythonCallInstruction>(
                callable, snapshot, std::span<const TaggedValueRef>{},
                BytecodePC{41});

        EXPECT_EQ(5u, call->operand_count());
        EXPECT_TRUE(call->operands_are_indirect());
        EXPECT_EQ(23u, call->slot(1));
        const uintptr_t *call_operands =
            reinterpret_cast<const uintptr_t *>(call->slot(0));
        EXPECT_EQ(callable.instruction(),
                  reinterpret_cast<Instruction *>(call_operands[0]));
        EXPECT_EQ(snapshot.instruction(),
                  reinterpret_cast<Instruction *>(call_operands[1]));
        EXPECT_EQ(2u, call_without_arguments->operand_count());
        EXPECT_TRUE(call_without_arguments->operands_are_indirect());
        EXPECT_EQ(41u, call_without_arguments->slot(1));
        EXPECT_EQ(3u, call->arguments().size());
        EXPECT_EQ(first.instruction(), call->arguments()[0].instruction());
        EXPECT_EQ(none.instruction(), call->arguments()[1].instruction());
        EXPECT_EQ(second.instruction(), call->arguments()[2].instruction());
        EXPECT_EQ(23u, call->interpreter_return_pc());
        EXPECT_EQ(0u, snapshot.instruction()->operand_count());
        EXPECT_TRUE(snapshot.instruction()->operands_are_indirect());
        EXPECT_EQ(0u, snapshot.instruction()->slot(0));
        EXPECT_EQ(23u, snapshot.instruction()->slot(1));

        std::vector<std::pair<OperandClass, Instruction *>> references;
        visit_operand_references(*call, [&](OperandClass operand_class,
                                            ValueRepresentation representation,
                                            Instruction *producer) {
            EXPECT_EQ(operand_class == OperandClass::ProgramValue
                          ? ValueRepresentation::TaggedValue
                          : ValueRepresentation::None,
                      representation);
            references.emplace_back(operand_class, producer);
        });

        ASSERT_EQ(5u, references.size());
        EXPECT_EQ(callable.instruction(), references[0].second);
        EXPECT_EQ(OperandClass::Snapshot, references[1].first);
        EXPECT_EQ(snapshot.instruction(), references[1].second);
        EXPECT_EQ(first.instruction(), references[2].second);
        EXPECT_EQ(none.instruction(), references[3].second);
        EXPECT_EQ(second.instruction(), references[4].second);
    }

    TEST(JitInstructionTraversal,
         SnapshotStoresPayloadsBeforeParallelDescriptors)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        TaggedValueRef tagged(builder.make_instruction<ParameterInstruction>());
        F64Ref f64(builder.make_instruction<ParameterF64Instruction>());
        std::array<SnapshotValue, 4> captured_values = {
            SnapshotValue(tagged), SnapshotValue(f64),
            SnapshotValue(InlineValueConstant(Value::True())),
            SnapshotValue::value_constant(Value::None())};

        EXPECT_EQ(8u, SnapshotInstruction::n_indirect_slots_for(
                          std::span<const SnapshotValue>(captured_values),
                          BytecodePC{91}));

        SnapshotInstruction *snapshot =
            builder.make_instruction<SnapshotInstruction>(
                std::span<const SnapshotValue>(captured_values),
                BytecodePC{91});

        ASSERT_EQ(4u, snapshot->operand_count());
        ASSERT_TRUE(snapshot->operands_are_indirect());
        ASSERT_NE(0u, snapshot->slot(0));
        const uintptr_t *storage =
            reinterpret_cast<const uintptr_t *>(snapshot->slot(0));
        EXPECT_EQ(tagged.instruction(),
                  reinterpret_cast<Instruction *>(storage[0]));
        EXPECT_EQ(f64.instruction(),
                  reinterpret_cast<Instruction *>(storage[1]));
        EXPECT_EQ(static_cast<uintptr_t>(SnapshotValueKind::ProgramValue),
                  storage[4]);
        EXPECT_EQ(static_cast<uintptr_t>(SnapshotValueKind::ProgramValue),
                  storage[5]);
        EXPECT_EQ(
            static_cast<uintptr_t>(SnapshotValueKind::InlineValueConstant),
            storage[6]);
        EXPECT_EQ(static_cast<uintptr_t>(SnapshotValueKind::ValueConstant),
                  storage[7]);

        SnapshotValuesView values = snapshot->captured_values();
        ASSERT_EQ(4u, values.size());
        EXPECT_EQ(SnapshotValueKind::ProgramValue, values[0].kind());
        EXPECT_EQ(tagged.instruction(),
                  values[0].program_value().instruction());
        EXPECT_EQ(f64.instruction(), values[1].program_value().instruction());
        EXPECT_EQ(Value::True(), values[2].inline_constant().value());
        EXPECT_EQ(Value::None(), values[3].value_constant());
        EXPECT_EQ(91u, snapshot->resume_pc());

        std::vector<Instruction *> references;
        visit_operand_references(
            *snapshot,
            [&](OperandClass operand_class, ValueRepresentation representation,
                Instruction *producer) {
                EXPECT_EQ(OperandClass::ProgramValue, operand_class);
                EXPECT_EQ(ValueRepresentation::None, representation);
                references.push_back(producer);
            });
        ASSERT_EQ(2u, references.size());
        EXPECT_EQ(tagged.instruction(), references[0]);
        EXPECT_EQ(f64.instruction(), references[1]);
    }

}  // namespace cl::jit
