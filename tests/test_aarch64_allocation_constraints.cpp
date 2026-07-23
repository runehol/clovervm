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
                      const Instruction *instruction)
        {
            for(const InstructionAllocationConstraints &candidate:
                constraints.instruction_overrides())
            {
                if(candidate.instruction() == instruction)
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
    }  // namespace

    TEST(AArch64AllocationConstraints,
         DefinesInitialGPRClassAndPlatformEntryABI)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *first =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction *second =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction *third =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction *return_instruction =
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

        const RegisterClassDefinition &simd = constraints.register_classes()[1];
        EXPECT_EQ(RegisterClass::SIMD, simd.register_class());
        EXPECT_EQ(24u, simd.members().size());
        EXPECT_TRUE(simd.members().contains(v(0)));
        EXPECT_TRUE(simd.members().contains(v(7)));
        EXPECT_FALSE(simd.members().contains(v(8)));
        EXPECT_FALSE(simd.members().contains(v(15)));
        EXPECT_TRUE(simd.members().contains(v(16)));
        EXPECT_TRUE(simd.members().contains(v(31)));
        ASSERT_EQ(24u, simd.allocation_order().size());
        EXPECT_EQ(v(0), simd.allocation_order().front());
        EXPECT_EQ(v(31), simd.allocation_order().back());

        ASSERT_EQ(4u, constraints.instruction_overrides().size());
        std::array parameters = {first, second, third};
        for(size_t index = 0; index < parameters.size(); ++index)
        {
            const InstructionAllocationConstraints *parameter =
                find_override(constraints, parameters[index]);
            ASSERT_NE(nullptr, parameter);
            ASSERT_TRUE(parameter->result_override().has_value());
            EXPECT_EQ(
                x(static_cast<uint8_t>(index)),
                parameter->result_override()->requirement.fixed_register());
        }

        const InstructionAllocationConstraints *return_override =
            find_override(constraints, return_instruction);
        ASSERT_NE(nullptr, return_override);
        ASSERT_EQ(1u, return_override->input_overrides().size());
        EXPECT_EQ(ReturnInstruction::return_value_operand_index,
                  return_override->input_overrides()[0].operand_index);
        EXPECT_EQ(
            x(0),
            return_override->input_overrides()[0].requirement.fixed_register());
    }

    TEST(AArch64AllocationConstraints,
         OmitsOrdinaryInstructionsAndConstrainsBranches)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *if_true = builder.emplace_block();
        Block *if_false = builder.emplace_block();
        ParameterInstruction *condition =
            builder.emplace_parameter<ParameterInstruction>(entry);
        BlockEdge *true_edge = builder.make_block_edge(entry, if_true);
        BlockEdge *false_edge = builder.make_block_edge(entry, if_false);

        TaggedValueRef lhs =
            emplace_constant(builder, entry, Value::from_smi(0b1010));
        TaggedValueRef rhs =
            emplace_constant(builder, entry, Value::from_smi(0b1100));
        AndSMIInstruction *and_instruction =
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs);
        OrrSMIInstruction *orr_instruction =
            builder.emplace_instruction<OrrSMIInstruction>(entry, lhs, rhs);
        EorSMIInstruction *eor_instruction =
            builder.emplace_instruction<EorSMIInstruction>(entry, lhs, rhs);
        ConditionalBranchInstruction *branch =
            builder.emplace_instruction<ConditionalBranchInstruction>(
                entry, TaggedValueRef(condition), true_edge, false_edge);
        ReturnInstruction *true_return =
            builder.emplace_instruction<ReturnInstruction>(
                if_true, emplace_constant(builder, if_true, Value::True()));
        ReturnInstruction *false_return =
            builder.emplace_instruction<ReturnInstruction>(
                if_false, emplace_constant(builder, if_false, Value::False()));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

        EXPECT_EQ(nullptr, find_override(constraints, lhs.instruction()));
        EXPECT_EQ(nullptr, find_override(constraints, rhs.instruction()));
        EXPECT_EQ(nullptr, find_override(constraints, and_instruction));
        EXPECT_EQ(nullptr, find_override(constraints, orr_instruction));
        EXPECT_EQ(nullptr, find_override(constraints, eor_instruction));

        const InstructionAllocationConstraints *branch_override =
            find_override(constraints, branch);
        ASSERT_NE(nullptr, branch_override);
        ASSERT_EQ(1u, branch_override->temporaries().size());
        EXPECT_EQ(RegisterRequirement::Kind::Any,
                  branch_override->temporaries()[0].requirement.kind());
        EXPECT_EQ(
            RegisterClass::GPR,
            branch_override->temporaries()[0].requirement.register_class());
        EXPECT_NE(nullptr, find_override(constraints, true_return));
        EXPECT_NE(nullptr, find_override(constraints, false_return));
    }

    TEST(AArch64AllocationConstraints,
         InternalBlockParametersUseDefaultConstraints)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        BlockEdge *edge = builder.make_block_edge(entry, exit);
        UnconditionalBranchInstruction *branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ParameterF64Instruction *f64_parameter =
            builder.emplace_parameter<ParameterF64Instruction>(exit);
        ReturnInstruction *return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                exit, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

        EXPECT_EQ(nullptr, find_override(constraints, parameter));
        EXPECT_EQ(nullptr, find_override(constraints, f64_parameter));
        EXPECT_NE(nullptr, find_override(constraints, branch));
        EXPECT_NE(nullptr, find_override(constraints, return_instruction));
    }

    TEST(AArch64AllocationConstraints, RejectsUnsupportedBringUpShapes)
    {
        {
            CompilationSession session;
            GraphBuilder builder(session);
            Block *entry = builder.emplace_block();
            builder.emplace_parameter<ParameterF64Instruction>(entry);
            builder.emplace_instruction<ReturnInstruction>(
                entry, emplace_constant(builder, entry));
            ControlFlowGraph *graph = builder.finalize();

            EXPECT_DEATH((void)make_aarch64_allocation_constraints(*graph),
                         "does not support F64 entry parameters");
        }

        {
            CompilationSession session;
            GraphBuilder builder(session);
            Block *entry = builder.emplace_block();
            ParameterInstruction *first = nullptr;
            for(size_t index = 0; index < 9; ++index)
            {
                ParameterInstruction *parameter =
                    builder.emplace_parameter<ParameterInstruction>(entry);
                if(index == 0)
                {
                    first = parameter;
                }
            }
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(first));
            ControlFlowGraph *graph = builder.finalize();

            EXPECT_DEATH((void)make_aarch64_allocation_constraints(*graph),
                         "does not support stack-passed entry parameters");
        }

        {
            CompilationSession session;
            GraphBuilder builder(session);
            Block *entry = builder.emplace_block();
            TaggedValueRef callable(
                builder.emplace_parameter<ParameterInstruction>(entry));
            SnapshotRef snapshot(
                builder.emplace_instruction<SnapshotInstruction>(
                    entry, std::span<const ProgramValueRef>{}, BytecodePC{5}));
            PythonCallInstruction *call =
                builder.emplace_instruction<PythonCallInstruction>(
                    entry, callable, snapshot,
                    std::span<const TaggedValueRef>{}, BytecodePC{5});
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(call));
            ControlFlowGraph *graph = builder.finalize();

            EXPECT_DEATH((void)make_aarch64_allocation_constraints(*graph),
                         "unsupported instruction");
        }
    }

}  // namespace cl::jit
