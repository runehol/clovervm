#include "jit/compilation_arena.h"
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
        Block *first_block = arena.make_block();
        Block *second_block = arena.make_block();
        ProgramValueRef first_instruction = arena.make_parameter();
        ProgramValueRef second_instruction = arena.make_parameter();

        EXPECT_EQ(0u, first_block->serial().value());
        EXPECT_EQ(1u, second_block->serial().value());
        EXPECT_EQ(0u, first_instruction.instruction()->serial().value());
        EXPECT_EQ(1u, second_instruction.instruction()->serial().value());
    }

    TEST(JitInstructionStorage, HasFiveSlotsAndTaggedStableAddresses)
    {
        static_assert(sizeof(Instruction) == 48);
        static_assert(std::is_trivially_destructible_v<Instruction>);

        CompilationArena arena;
        std::vector<Instruction *> instructions;
        for(size_t index = 0; index < 256; ++index)
        {
            instructions.push_back(arena.make_parameter().instruction());
        }

        for(size_t index = 0; index < instructions.size(); ++index)
        {
            Instruction *instruction = instructions[index];
            EXPECT_EQ(index, instruction->serial().value());
            EXPECT_NE(0u, reinterpret_cast<uintptr_t>(instruction) &
                              value_ptr_mask);
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

    TEST(JitInstructionTraversal, DistinguishesReferencesConstantsAndSnapshots)
    {
        CompilationArena arena;
        ProgramValueRef lhs = arena.make_parameter();
        SnapshotRef snapshot = arena.make_snapshot(17);
        ProgramValueRef add = arena.make_add_smi(
            ProgramValueOperand(lhs),
            ProgramValueOperand(InlineValueConstant(Value::from_smi(3))),
            snapshot);

        EXPECT_EQ(3u, add.instruction()->operand_count());
        EXPECT_FALSE(add.instruction()->operands_are_indirect());

        std::vector<std::pair<OperandClass, Instruction *>> references;
        visit_operand_references(
            *add.instruction(),
            [&](OperandClass operand_class, Instruction *producer) {
                references.emplace_back(operand_class, producer);
            });

        ASSERT_EQ(2u, references.size());
        EXPECT_EQ(OperandClass::ProgramValue, references[0].first);
        EXPECT_EQ(lhs.instruction(), references[0].second);
        EXPECT_EQ(OperandClass::Snapshot, references[1].first);
        EXPECT_EQ(snapshot.instruction(), references[1].second);
    }

    TEST(JitInstructionTraversal, WalksVariadicPythonCallArguments)
    {
        CompilationArena arena;
        ProgramValueRef callable = arena.make_parameter();
        ProgramValueRef first = arena.make_parameter();
        ProgramValueRef second = arena.make_parameter();
        SnapshotRef snapshot = arena.make_snapshot(23);
        std::array<ProgramValueOperand, 3> arguments = {
            ProgramValueOperand(first),
            ProgramValueOperand(InlineValueConstant(Value::None())),
            ProgramValueOperand(second)};
        ProgramValueRef call = arena.make_python_call(
            ProgramValueOperand(callable), arguments, snapshot, 23);
        ProgramValueRef call_without_arguments = arena.make_python_call(
            ProgramValueOperand(callable), {}, snapshot, 41);

        EXPECT_EQ(5u, call.instruction()->operand_count());
        EXPECT_TRUE(call.instruction()->operands_are_indirect());
        EXPECT_EQ(23u, call.instruction()->slot(1));
        const uintptr_t *call_operands =
            reinterpret_cast<const uintptr_t *>(call.instruction()->slot(0));
        EXPECT_EQ(callable.instruction(),
                  reinterpret_cast<Instruction *>(call_operands[0]));
        EXPECT_EQ(snapshot.instruction(),
                  reinterpret_cast<Instruction *>(call_operands[1]));
        EXPECT_EQ(2u, call_without_arguments.instruction()->operand_count());
        EXPECT_TRUE(
            call_without_arguments.instruction()->operands_are_indirect());
        EXPECT_EQ(41u, call_without_arguments.instruction()->slot(1));
        EXPECT_EQ(0u, snapshot.instruction()->operand_count());
        EXPECT_TRUE(snapshot.instruction()->operands_are_indirect());
        EXPECT_EQ(0u, snapshot.instruction()->slot(0));
        EXPECT_EQ(23u, snapshot.instruction()->slot(1));

        std::vector<std::pair<OperandClass, Instruction *>> references;
        visit_operand_references(
            *call.instruction(),
            [&](OperandClass operand_class, Instruction *producer) {
                references.emplace_back(operand_class, producer);
            });

        ASSERT_EQ(4u, references.size());
        EXPECT_EQ(callable.instruction(), references[0].second);
        EXPECT_EQ(OperandClass::Snapshot, references[1].first);
        EXPECT_EQ(snapshot.instruction(), references[1].second);
        EXPECT_EQ(first.instruction(), references[2].second);
        EXPECT_EQ(second.instruction(), references[3].second);
    }

}  // namespace cl::jit
