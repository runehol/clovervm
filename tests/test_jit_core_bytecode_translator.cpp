#include "jit/core_bytecode_translator.h"

#include "bytecode/code_object_builder.h"
#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/core_ir_optimization.h"
#include "jit/instruction.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct TranslatorFixture
        {
            TranslatorFixture()
                : activation_scope(context.thread()),
                  name(context.vm().get_or_create_interned_string_value(
                      L"<jit-core-translation-test>")),
                  code_builder(&context.vm(), nullptr,
                               TValue<ModuleObject>::from_oop(
                                   context.make_test_module_object(
                                       name, context.vm()
                                                 .global_builtins_module()
                                                 .raw_value())),
                               nullptr, name),
                  graph_builder(session, IRLevel::Core)
            {
            }

            ControlFlowGraph *translate()
            {
                CodeObject *code_object = code_builder.finalize().value();
                CoreBytecodeTranslator translator(*code_object, graph_builder);
                return translator.translate();
            }

            test::VmTestContext context;
            ThreadState::ActivationScope activation_scope;
            TValue<String> name;
            CodeObjectBuilder code_builder;
            CompilationSession session;
            GraphBuilder graph_builder;
        };

        std::vector<Instruction> instructions_of_kind(Block *block,
                                                      InstructionKind kind)
        {
            std::vector<Instruction> matches;
            for(Instruction instruction: block->instructions())
            {
                if(instruction.kind() == kind)
                {
                    matches.push_back(instruction);
                }
            }
            return matches;
        }

        size_t bytecode_state_size(const ControlFlowGraph &graph)
        {
            EXPECT_TRUE(graph.bytecode_state_order().has_value());
            return graph.bytecode_state_order()->size();
        }
    }  // namespace

    TEST(JitCoreBytecodeTranslator,
         ConstantsAndRegisterTransfersPreserveProgramValueIdentity)
    {
        TranslatorFixture fixture;
        uint32_t constant_index =
            fixture.code_builder.allocate_constant(fixture.name.raw_value())
                .value();
        {
            CodeObjectBuilder::TemporaryReg temporaries(fixture.code_builder,
                                                        2);
            fixture.code_builder.emit_lda_smi(0, 37).value();
            fixture.code_builder.emit_star(0, temporaries).value();
            fixture.code_builder.emit_lda_none(0).value();
            fixture.code_builder.emit_lda_constant(0, uint8_t(constant_index))
                .value();
            fixture.code_builder
                .emit_mov(0, uint32_t(temporaries) + 1, temporaries)
                .value();
            fixture.code_builder.emit_ldar(0, uint32_t(temporaries) + 1)
                .value();
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(1u, graph->blocks().size());
        Block *entry = graph->entry_block();

        std::vector<Instruction> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(3u, constants.size());
        EXPECT_EQ(Value::from_smi(37),
                  constants[0].as<ConstInstruction>().constant());
        EXPECT_EQ(Value::None(),
                  constants[1].as<ConstInstruction>().constant());
        EXPECT_EQ(fixture.name.raw_value(),
                  constants[2].as<ConstInstruction>().constant());
        EXPECT_TRUE(instructions_of_kind(entry, InstructionKind::Mov).empty());

        ReturnInstruction return_instruction =
            entry->instruction_at(entry->instructions().size() - 1)
                .as<ReturnInstruction>();
        EXPECT_EQ(constants[0].id(),
                  return_instruction.return_value().instruction_id());
    }

    TEST(JitCoreBytecodeTranslator, TranslatesIdentityTests)
    {
        TranslatorFixture fixture;
        {
            CodeObjectBuilder::TemporaryReg temporaries(fixture.code_builder,
                                                        2);
            fixture.code_builder.emit_lda_true(0).value();
            fixture.code_builder.emit_star(0, temporaries).value();
            fixture.code_builder.emit_lda_false(0).value();
            fixture.code_builder.emit_star(0, uint32_t(temporaries) + 1)
                .value();
            fixture.code_builder
                .emit_operator_reg(0, Bytecode::TestIs, temporaries,
                                   OperatorBytecodeFormat::Plain)
                .value();
            fixture.code_builder.emit_ldar(0, uint32_t(temporaries) + 1)
                .value();
            fixture.code_builder
                .emit_operator_reg(0, Bytecode::TestIsNot, temporaries,
                                   OperatorBytecodeFormat::Plain)
                .value();
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        std::vector<Instruction> is_instructions =
            instructions_of_kind(entry, InstructionKind::Is);
        std::vector<Instruction> is_not_instructions =
            instructions_of_kind(entry, InstructionKind::IsNot);
        ASSERT_EQ(2u, constants.size());
        ASSERT_EQ(1u, is_instructions.size());
        ASSERT_EQ(1u, is_not_instructions.size());

        IsInstruction is = is_instructions.front().as<IsInstruction>();
        EXPECT_EQ(constants[0].id(), is.lhs().instruction_id());
        EXPECT_EQ(constants[1].id(), is.rhs().instruction_id());
        IsNotInstruction is_not =
            is_not_instructions.front().as<IsNotInstruction>();
        EXPECT_EQ(constants[0].id(), is_not.lhs().instruction_id());
        EXPECT_EQ(constants[1].id(), is_not.rhs().instruction_id());
    }

    TEST(JitCoreBytecodeTranslator, LowersAddWithAnEmptyCacheToGuardedSMIAdd)
    {
        TranslatorFixture fixture;
        uint32_t add_pc;
        {
            CodeObjectBuilder::TemporaryReg temporaries(fixture.code_builder,
                                                        1);
            fixture.code_builder.emit_lda_smi(0, 19).value();
            fixture.code_builder.emit_star(0, temporaries).value();
            fixture.code_builder.emit_lda_smi(0, 23).value();
            add_pc =
                fixture.code_builder
                    .emit_operator_reg(
                        0, Bytecode::Add, temporaries,
                        OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                    .value();
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        std::vector<Instruction> guards =
            instructions_of_kind(entry, InstructionKind::InlineTagGuard);
        std::vector<Instruction> adds =
            instructions_of_kind(entry, InstructionKind::AddSMI);
        std::vector<Instruction> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(1u, snapshots.size());
        ASSERT_EQ(2u, guards.size());
        ASSERT_EQ(1u, adds.size());
        ASSERT_EQ(2u, constants.size());

        SnapshotInstruction snapshot =
            snapshots.front().as<SnapshotInstruction>();
        EXPECT_EQ(add_pc, snapshot.resume_pc_offset());
        InlineTagGuardInstruction lhs =
            guards[0].as<InlineTagGuardInstruction>();
        InlineTagGuardInstruction rhs =
            guards[1].as<InlineTagGuardInstruction>();
        EXPECT_EQ(TaggedValueClass::smi(), lhs.expected_class());
        EXPECT_EQ(TaggedValueClass::smi(), rhs.expected_class());
        EXPECT_EQ(snapshot.id(), lhs.snapshot().instruction_id());
        EXPECT_EQ(snapshot.id(), rhs.snapshot().instruction_id());
        EXPECT_EQ(constants[0].id(), lhs.value().instruction_id());
        EXPECT_EQ(constants[1].id(), rhs.value().instruction_id());

        AddSMIInstruction add = adds.front().as<AddSMIInstruction>();
        EXPECT_EQ(lhs.id(), add.lhs().instruction_id());
        EXPECT_EQ(rhs.id(), add.rhs().instruction_id());
        EXPECT_EQ(snapshot.id(), add.snapshot().instruction_id());
        ReturnInstruction return_instruction =
            entry->instruction_at(entry->instructions().size() - 1)
                .as<ReturnInstruction>();
        EXPECT_EQ(add.id(), return_instruction.return_value().instruction_id());
    }

    TEST(JitCoreBytecodeTranslator, LeavesAddWithAPopulatedCacheUnsupported)
    {
        TranslatorFixture fixture;
        uint32_t add_pc;
        {
            CodeObjectBuilder::TemporaryReg temporaries(fixture.code_builder,
                                                        1);
            fixture.code_builder.emit_lda_smi(0, 19).value();
            fixture.code_builder.emit_star(0, temporaries).value();
            fixture.code_builder.emit_lda_smi(0, 23).value();
            add_pc =
                fixture.code_builder
                    .emit_operator_reg(
                        0, Bytecode::Add, temporaries,
                        OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                    .value();
            fixture.code_builder.emit_return(0).value();
        }
        CodeObject *code_object = fixture.code_builder.finalize().value();
        ASSERT_EQ(1u, code_object->inline_caches.operator_caches.size());
        code_object->inline_caches.operator_caches[0].populate_binary_shapes(
            ShapeKey::from_value(Value::from_smi(19)),
            ShapeKey::from_value(Value::from_smi(23)));

        CoreBytecodeTranslator translator(*code_object, fixture.graph_builder);
        ControlFlowGraph *graph = translator.translate();
        Block *entry = graph->entry_block();
        EXPECT_TRUE(instructions_of_kind(entry, InstructionKind::InlineTagGuard)
                        .empty());
        EXPECT_TRUE(
            instructions_of_kind(entry, InstructionKind::AddSMI).empty());
        std::vector<Instruction> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        std::vector<Instruction> resumes =
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter);
        ASSERT_EQ(1u, snapshots.size());
        ASSERT_EQ(1u, resumes.size());
        EXPECT_EQ(
            add_pc,
            snapshots.front().as<SnapshotInstruction>().resume_pc_offset());
        EXPECT_EQ(resumes.front().id(),
                  entry->instruction_at(entry->instructions().size() - 1).id());
    }

    TEST(JitCoreBytecodeTranslator, LowersAddSmiWithAnEmptyCacheToGuardedSMIAdd)
    {
        TranslatorFixture fixture;
        fixture.code_builder.emit_lda_smi(0, 19).value();
        uint32_t add_pc =
            fixture.code_builder
                .emit_operator_smi(
                    0, Bytecode::AddSmi, -23,
                    OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                .value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        std::vector<Instruction> guards =
            instructions_of_kind(entry, InstructionKind::InlineTagGuard);
        std::vector<Instruction> adds =
            instructions_of_kind(entry, InstructionKind::AddSMI);
        std::vector<Instruction> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(1u, snapshots.size());
        ASSERT_EQ(1u, guards.size());
        ASSERT_EQ(1u, adds.size());
        ASSERT_EQ(2u, constants.size());

        SnapshotInstruction snapshot =
            snapshots.front().as<SnapshotInstruction>();
        EXPECT_EQ(add_pc, snapshot.resume_pc_offset());
        InlineTagGuardInstruction lhs =
            guards.front().as<InlineTagGuardInstruction>();
        EXPECT_EQ(TaggedValueClass::smi(), lhs.expected_class());
        EXPECT_EQ(snapshot.id(), lhs.snapshot().instruction_id());
        EXPECT_EQ(constants[0].id(), lhs.value().instruction_id());
        EXPECT_EQ(Value::from_smi(-23),
                  constants[1].as<ConstInstruction>().constant());

        AddSMIInstruction add = adds.front().as<AddSMIInstruction>();
        EXPECT_EQ(lhs.id(), add.lhs().instruction_id());
        EXPECT_EQ(constants[1].id(), add.rhs().instruction_id());
        EXPECT_EQ(snapshot.id(), add.snapshot().instruction_id());
        ReturnInstruction return_instruction =
            entry->instruction_at(entry->instructions().size() - 1)
                .as<ReturnInstruction>();
        EXPECT_EQ(add.id(), return_instruction.return_value().instruction_id());
    }

    TEST(JitCoreBytecodeTranslator,
         ReusesGuardedDefinitionForAliasedOperatorInputs)
    {
        TranslatorFixture fixture;
        fixture.code_builder.n_parameters() = 1;
        fixture.code_builder.n_positional_parameters() = 1;
        fixture.code_builder.emit_ldar(0, 0).value();
        fixture.code_builder
            .emit_operator_reg(
                0, Bytecode::Add, 0,
                OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
            .value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> guards =
            instructions_of_kind(entry, InstructionKind::InlineTagGuard);
        ASSERT_EQ(2u, guards.size());
        InlineTagGuardInstruction first =
            guards[0].as<InlineTagGuardInstruction>();
        InlineTagGuardInstruction second =
            guards[1].as<InlineTagGuardInstruction>();
        EXPECT_EQ(first.id(), second.value().instruction_id());

        auto optimization = optimize_core_ir(fixture.session, *graph);

        ASSERT_TRUE(optimization);
        EXPECT_TRUE(std::move(optimization).value());
        EXPECT_FALSE(first.is_poisoned());
        EXPECT_TRUE(second.is_poisoned());
    }

    TEST(JitCoreBytecodeTranslator,
         LowersSubAndMulThroughBinaryArithmeticFamily)
    {
        TranslatorFixture fixture;
        {
            CodeObjectBuilder::TemporaryReg temporary(fixture.code_builder, 1);
            fixture.code_builder.emit_lda_smi(0, 19).value();
            fixture.code_builder.emit_star(0, temporary).value();
            fixture.code_builder.emit_lda_smi(0, 23).value();
            fixture.code_builder
                .emit_operator_reg(
                    0, Bytecode::Sub, temporary,
                    OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                .value();
            fixture.code_builder
                .emit_operator_smi(
                    0, Bytecode::SubSmi, -3,
                    OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                .value();
            fixture.code_builder
                .emit_operator_reg(
                    0, Bytecode::Mul, temporary,
                    OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                .value();
            fixture.code_builder
                .emit_operator_smi(
                    0, Bytecode::MulSmi, -3,
                    OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                .value();
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> subtracts =
            instructions_of_kind(entry, InstructionKind::SubSMI);
        ASSERT_EQ(2u, subtracts.size());
        EXPECT_EQ(BinaryArithmeticSMIWithSnapshotSubkind::SubSMI,
                  subtracts[0]
                      .as<BinaryArithmeticSMIWithSnapshotInstruction>()
                      .subkind());
        EXPECT_EQ(BinaryArithmeticSMIWithSnapshotSubkind::SubSMI,
                  subtracts[1]
                      .as<BinaryArithmeticSMIWithSnapshotInstruction>()
                      .subkind());
        std::vector<Instruction> multiplies =
            instructions_of_kind(entry, InstructionKind::MulSMI);
        ASSERT_EQ(2u, multiplies.size());
        EXPECT_EQ(BinaryArithmeticSMIWithSnapshotSubkind::MulSMI,
                  multiplies[0]
                      .as<BinaryArithmeticSMIWithSnapshotInstruction>()
                      .subkind());
        EXPECT_EQ(BinaryArithmeticSMIWithSnapshotSubkind::MulSMI,
                  multiplies[1]
                      .as<BinaryArithmeticSMIWithSnapshotInstruction>()
                      .subkind());
        EXPECT_EQ(
            4u, instructions_of_kind(entry, InstructionKind::Snapshot).size());
        EXPECT_EQ(6u,
                  instructions_of_kind(entry, InstructionKind::InlineTagGuard)
                      .size());
    }

    TEST(JitCoreBytecodeTranslator,
         LowersBinaryLogicalBytecodesThroughTheirSMIFamily)
    {
        TranslatorFixture fixture;
        {
            CodeObjectBuilder::TemporaryReg temporary(fixture.code_builder, 1);
            fixture.code_builder.emit_lda_smi(0, 13).value();
            fixture.code_builder.emit_star(0, temporary).value();
            fixture.code_builder.emit_lda_smi(0, 7).value();
            for(Bytecode opcode: {Bytecode::And, Bytecode::Or, Bytecode::Xor})
            {
                fixture.code_builder
                    .emit_operator_reg(
                        0, opcode, temporary,
                        OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                    .value();
            }
            for(Bytecode opcode:
                {Bytecode::AndSmi, Bytecode::OrSmi, Bytecode::XorSmi})
            {
                fixture.code_builder
                    .emit_operator_smi(
                        0, opcode, 3,
                        OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                    .value();
            }
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        EXPECT_EQ(2u,
                  instructions_of_kind(entry, InstructionKind::AndSMI).size());
        EXPECT_EQ(2u,
                  instructions_of_kind(entry, InstructionKind::OrrSMI).size());
        EXPECT_EQ(2u,
                  instructions_of_kind(entry, InstructionKind::EorSMI).size());
        EXPECT_EQ(
            6u, instructions_of_kind(entry, InstructionKind::Snapshot).size());
        EXPECT_EQ(9u,
                  instructions_of_kind(entry, InstructionKind::InlineTagGuard)
                      .size());
        EXPECT_TRUE(
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter)
                .empty());
    }

    TEST(JitCoreBytecodeTranslator,
         PopulatedLogicalOperatorCacheExitsToInterpreter)
    {
        TranslatorFixture fixture;
        uint32_t operator_pc;
        {
            CodeObjectBuilder::TemporaryReg temporary(fixture.code_builder, 1);
            fixture.code_builder.emit_lda_smi(0, 13).value();
            fixture.code_builder.emit_star(0, temporary).value();
            fixture.code_builder.emit_lda_smi(0, 7).value();
            operator_pc =
                fixture.code_builder
                    .emit_operator_reg(
                        0, Bytecode::And, temporary,
                        OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                    .value();
            fixture.code_builder.emit_return(0).value();
        }
        CodeObject *code_object = fixture.code_builder.finalize().value();
        ASSERT_EQ(1u, code_object->inline_caches.operator_caches.size());
        code_object->inline_caches.operator_caches[0].populate_binary_shapes(
            ShapeKey::from_value(Value::from_smi(13)),
            ShapeKey::from_value(Value::from_smi(7)));

        CoreBytecodeTranslator translator(*code_object, fixture.graph_builder);
        ControlFlowGraph *graph = translator.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        std::vector<Instruction> resumes =
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter);
        ASSERT_EQ(1u, snapshots.size());
        ASSERT_EQ(1u, resumes.size());
        EXPECT_EQ(
            operator_pc,
            snapshots.front().as<SnapshotInstruction>().resume_pc_offset());
        EXPECT_EQ(resumes.front().id(),
                  entry->instruction_at(entry->instructions().size() - 1).id());
        EXPECT_TRUE(instructions_of_kind(entry, InstructionKind::InlineTagGuard)
                        .empty());
        EXPECT_TRUE(
            instructions_of_kind(entry, InstructionKind::AndSMI).empty());
    }

    TEST(JitCoreBytecodeTranslator,
         LowersComparisonBytecodesThroughTheirSMIFamily)
    {
        TranslatorFixture fixture;
        const std::array bytecodes = {
            Bytecode::TestEqual,   Bytecode::TestNotEqual,
            Bytecode::TestLess,    Bytecode::TestLessEqual,
            Bytecode::TestGreater, Bytecode::TestGreaterEqual,
        };
        const std::array expected_subkinds = {
            BinaryComparisonSMISubkind::EqualSMI,
            BinaryComparisonSMISubkind::NotEqualSMI,
            BinaryComparisonSMISubkind::LessSMI,
            BinaryComparisonSMISubkind::LessEqualSMI,
            BinaryComparisonSMISubkind::GreaterSMI,
            BinaryComparisonSMISubkind::GreaterEqualSMI,
        };
        {
            CodeObjectBuilder::TemporaryReg temporary(fixture.code_builder, 1);
            fixture.code_builder.emit_lda_smi(0, 13).value();
            fixture.code_builder.emit_star(0, temporary).value();
            fixture.code_builder.emit_lda_smi(0, 7).value();
            for(Bytecode opcode: bytecodes)
            {
                fixture.code_builder
                    .emit_operator_reg(
                        0, opcode, temporary,
                        OperatorBytecodeFormat::WithCacheAndNotImplementedCheck)
                    .value();
            }
            fixture.code_builder.emit_return(0).value();
        }

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<BinaryComparisonSMISubkind> actual_subkinds;
        for(Instruction instruction: entry->instructions())
        {
            if(BinaryComparisonSMIInstruction::accepts_kind(instruction.kind()))
            {
                actual_subkinds.push_back(
                    instruction.as<BinaryComparisonSMIInstruction>().subkind());
            }
        }
        EXPECT_EQ(
            std::vector(expected_subkinds.begin(), expected_subkinds.end()),
            actual_subkinds);
        EXPECT_EQ(
            6u, instructions_of_kind(entry, InstructionKind::Snapshot).size());
        EXPECT_EQ(12u,
                  instructions_of_kind(entry, InstructionKind::InlineTagGuard)
                      .size());
        EXPECT_TRUE(
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter)
                .empty());
    }

    TEST(JitCoreBytecodeTranslator,
         JumpIfFalseMapsFallthroughToTrueAndJumpToFalse)
    {
        TranslatorFixture fixture;
        JumpTarget jump_target(&fixture.code_builder);
        fixture.code_builder.emit_lda_true(0).value();
        fixture.code_builder.emit_jump_if_false(0, jump_target).value();
        fixture.code_builder.emit_lda_smi(0, 1).value();
        fixture.code_builder.emit_return(0).value();
        jump_target.resolve().value();
        fixture.code_builder.emit_lda_smi(0, 2).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(3u, graph->blocks().size());
        Block *entry = graph->blocks()[0];
        Block *fallthrough = graph->blocks()[1];
        Block *jump = graph->blocks()[2];
        std::vector<Instruction> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        std::vector<Instruction> guards =
            instructions_of_kind(entry, InstructionKind::InlineTagGuard);
        ASSERT_EQ(1u, snapshots.size());
        ASSERT_EQ(1u, guards.size());
        SnapshotInstruction snapshot =
            snapshots.front().as<SnapshotInstruction>();
        InlineTagGuardInstruction guard =
            guards.front().as<InlineTagGuardInstruction>();
        ConditionalBranchInstruction branch =
            entry->instruction_at(entry->instructions().size() - 1)
                .as<ConditionalBranchInstruction>();

        EXPECT_EQ(1u, snapshot.resume_pc_offset());
        EXPECT_EQ(TaggedValueClass::any_inline(), guard.expected_class());
        EXPECT_EQ(snapshot.id(), guard.snapshot().instruction_id());
        EXPECT_EQ(snapshot.captured_values()[0].instruction_id(),
                  guard.value().instruction_id());
        EXPECT_EQ(guard.id(), branch.condition().instruction_id());
        EXPECT_EQ(fallthrough, branch.true_edge()->target());
        EXPECT_EQ(jump, branch.false_edge()->target());
        size_t state_size = bytecode_state_size(*graph);
        ASSERT_EQ(state_size, branch.true_edge()->arguments().size());
        ASSERT_EQ(state_size, branch.false_edge()->arguments().size());
        EXPECT_EQ(branch.condition().instruction_id(),
                  branch.true_edge()->arguments()[0].instruction_id());
        EXPECT_EQ(branch.condition().instruction_id(),
                  branch.false_edge()->arguments()[0].instruction_id());
        EXPECT_EQ(state_size, fallthrough->parameters().size());
        EXPECT_EQ(state_size, jump->parameters().size());
        EXPECT_EQ(
            InstructionKind::Parameter,
            fallthrough->parameter_at(BytecodeStateOrder::AccumulatorPosition)
                .kind());
        EXPECT_EQ(
            InstructionKind::Parameter,
            jump->parameter_at(BytecodeStateOrder::AccumulatorPosition).kind());

        const BytecodeStateOrder &state_order =
            graph->bytecode_state_order().value();
        for(Block *successor: {fallthrough, jump})
        {
            ReturnInstruction return_instruction =
                successor->instruction_at(successor->instructions().size() - 1)
                    .as<ReturnInstruction>();
            EXPECT_EQ(InstructionKind::ParameterPointer,
                      successor
                          ->parameter_at(state_order.position_for_frame_offset(
                              FrameHeaderPreviousFpOffset))
                          .kind());
            EXPECT_EQ(InstructionKind::ParameterPointer,
                      successor
                          ->parameter_at(state_order.position_for_frame_offset(
                              FrameHeaderCompiledReturnPcOffset))
                          .kind());
            EXPECT_EQ(InstructionKind::Parameter,
                      successor
                          ->parameter_at(state_order.position_for_frame_offset(
                              FrameHeaderReturnCodeObjectOffset))
                          .kind());
            EXPECT_EQ(InstructionKind::ParameterPointer,
                      successor
                          ->parameter_at(state_order.position_for_frame_offset(
                              FrameHeaderReturnPcOffset))
                          .kind());
            EXPECT_EQ(
                successor
                    ->parameter_at(state_order.position_for_frame_offset(
                        FrameHeaderPreviousFpOffset))
                    .id(),
                return_instruction.previous_frame_pointer().instruction_id());
            EXPECT_EQ(successor
                          ->parameter_at(state_order.position_for_frame_offset(
                              FrameHeaderReturnCodeObjectOffset))
                          .id(),
                      return_instruction.return_code_object().instruction_id());
            EXPECT_EQ(successor
                          ->parameter_at(state_order.position_for_frame_offset(
                              FrameHeaderReturnPcOffset))
                          .id(),
                      return_instruction.return_pc().instruction_id());
        }
    }

    TEST(JitCoreBytecodeTranslator,
         JumpIfTrueMapsJumpToTrueAndFallthroughToFalse)
    {
        TranslatorFixture fixture;
        JumpTarget jump_target(&fixture.code_builder);
        fixture.code_builder.emit_lda_false(0).value();
        fixture.code_builder.emit_jump_if_true(0, jump_target).value();
        fixture.code_builder.emit_lda_smi(0, 1).value();
        fixture.code_builder.emit_return(0).value();
        jump_target.resolve().value();
        fixture.code_builder.emit_lda_smi(0, 2).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(3u, graph->blocks().size());
        Block *entry = graph->blocks()[0];
        ConditionalBranchInstruction branch =
            entry->instruction_at(entry->instructions().size() - 1)
                .as<ConditionalBranchInstruction>();

        EXPECT_EQ(graph->blocks()[2], branch.true_edge()->target());
        EXPECT_EQ(graph->blocks()[1], branch.false_edge()->target());
    }

    TEST(JitCoreBytecodeTranslator,
         SequentialBoundaryAndLoopBackedgeCarryCompleteState)
    {
        TranslatorFixture fixture;
        fixture.code_builder.emit_lda_smi(0, 0).value();
        JumpTarget loop(&fixture.code_builder);
        loop.resolve().value();
        fixture.code_builder.emit_lda_true(0).value();
        fixture.code_builder.emit_jump_if_true(0, loop).value();
        fixture.code_builder.emit_lda_none(0).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        ASSERT_EQ(3u, graph->blocks().size());
        Block *entry = graph->blocks()[0];
        Block *loop_block = graph->blocks()[1];

        UnconditionalBranchInstruction entry_branch =
            entry->instruction_at(entry->instructions().size() - 1)
                .as<UnconditionalBranchInstruction>();
        EXPECT_EQ(loop_block, entry_branch.edge()->target());
        size_t state_size = bytecode_state_size(*graph);
        ASSERT_EQ(state_size, entry_branch.edge()->arguments().size());
        ASSERT_EQ(state_size, loop_block->parameters().size());

        ConditionalBranchInstruction loop_branch =
            loop_block->instruction_at(loop_block->instructions().size() - 1)
                .as<ConditionalBranchInstruction>();
        EXPECT_EQ(loop_block, loop_branch.true_edge()->target());
        ASSERT_EQ(state_size, loop_branch.true_edge()->arguments().size());
        ASSERT_EQ(2u, loop_block->predecessor_edges().size());
        EXPECT_EQ(entry_branch.edge(), loop_block->predecessor_edges()[0]);
        EXPECT_EQ(loop_branch.true_edge(), loop_block->predecessor_edges()[1]);
    }

    TEST(JitCoreBytecodeTranslator,
         UnsupportedSequentialInstructionSnapshotsAndTerminatesBlock)
    {
        TranslatorFixture fixture;
        fixture.code_builder.emit_lda_smi(0, 7).value();
        fixture.code_builder.emit_to_bool(0).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        std::vector<Instruction> resumes =
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter);
        std::vector<Instruction> uninitialized =
            instructions_of_kind(entry, InstructionKind::Uninitialized);
        ASSERT_EQ(1u, snapshots.size());
        ASSERT_EQ(1u, resumes.size());
        ASSERT_EQ(2u, uninitialized.size());

        SnapshotInstruction snapshot =
            snapshots.front().as<SnapshotInstruction>();
        EXPECT_EQ(2u, snapshot.resume_pc_offset());
        ASSERT_EQ(bytecode_state_size(*graph),
                  snapshot.captured_values().size());
        std::vector<Instruction> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(1u, constants.size());
        EXPECT_EQ(constants.front().id(),
                  snapshot.captured_values()[0].instruction_id());
        EXPECT_EQ(snapshot.id(), resumes.front()
                                     .as<ResumeInInterpreterInstruction>()
                                     .snapshot()
                                     .instruction_id());
        EXPECT_EQ(resumes.front().id(),
                  entry->instruction_at(entry->instructions().size() - 1).id());
        EXPECT_TRUE(resumes.front().is_block_terminator());
        EXPECT_TRUE(entry->block_successor_edges().empty());
    }

    TEST(JitCoreBytecodeTranslator,
         EntryParameterAliasesIntoCompleteSnapshotState)
    {
        TranslatorFixture fixture;
        fixture.code_builder.n_parameters() = 1;
        fixture.code_builder.n_positional_parameters() = 1;
        fixture.code_builder.emit_ldar(0, 0).value();
        fixture.code_builder.emit_to_bool(0).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        ASSERT_EQ(1u + FrameHeaderSize, entry->parameters().size());
        Instruction parameter = entry->parameter_at(0);
        EXPECT_EQ(InstructionKind::Parameter, parameter.kind());
        EXPECT_EQ(InstructionKind::ParameterPointer,
                  entry->parameter_at(1).kind());
        EXPECT_EQ(InstructionKind::ParameterPointer,
                  entry->parameter_at(2).kind());
        EXPECT_EQ(InstructionKind::Parameter, entry->parameter_at(3).kind());
        EXPECT_EQ(InstructionKind::ParameterPointer,
                  entry->parameter_at(4).kind());
        std::vector<Instruction> snapshots =
            instructions_of_kind(entry, InstructionKind::Snapshot);
        ASSERT_EQ(1u, snapshots.size());
        ProgramValueRefRange captured =
            snapshots.front().as<SnapshotInstruction>().captured_values();
        ASSERT_EQ(bytecode_state_size(*graph), captured.size());
        EXPECT_EQ(parameter.id(), captured[0].instruction_id());
        EXPECT_EQ(parameter.id(), captured[1].instruction_id());
        const BytecodeStateOrder &state_order =
            graph->bytecode_state_order().value();
        for(int32_t frame_offset = FrameHeaderPreviousFpOffset;
            frame_offset <= FrameHeaderReturnPcOffset; ++frame_offset)
        {
            EXPECT_EQ(
                entry
                    ->parameter_at(
                        1 + size_t(frame_offset - FrameHeaderPreviousFpOffset))
                    .id(),
                captured[state_order.position_for_frame_offset(frame_offset)]
                    .instruction_id());
        }
    }

    TEST(JitCoreBytecodeTranslator,
         UnsupportedConditionalSnapshotsAndTerminatesBlock)
    {
        TranslatorFixture fixture;
        JumpTarget jump_target(&fixture.code_builder);
        fixture.code_builder.emit_lda_smi(0, 7).value();
        fixture.code_builder.emit_jump_if_equal_smi(0, 7, jump_target).value();
        fixture.code_builder.emit_lda_smi(0, 1).value();
        fixture.code_builder.emit_return(0).value();
        jump_target.resolve().value();
        fixture.code_builder.emit_lda_smi(0, 2).value();
        fixture.code_builder.emit_return(0).value();

        ControlFlowGraph *graph = fixture.translate();
        Block *entry = graph->entry_block();
        std::vector<Instruction> resumes =
            instructions_of_kind(entry, InstructionKind::ResumeInInterpreter);
        ASSERT_EQ(1u, resumes.size());
        EXPECT_EQ(resumes.front().id(),
                  entry->instruction_at(entry->instructions().size() - 1).id());
        EXPECT_TRUE(resumes.front().is_block_terminator());
        EXPECT_TRUE(entry->block_successor_edges().empty());

        std::vector<Instruction> constants =
            instructions_of_kind(entry, InstructionKind::Const);
        ASSERT_EQ(1u, constants.size());
    }

}  // namespace cl::jit
