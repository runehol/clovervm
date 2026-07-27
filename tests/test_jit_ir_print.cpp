#include "jit/ir_print.h"

#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/instruction.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>

namespace cl::jit
{
    TEST(JitIRPrint, PrintsDenseValuesAndPositionalOperands)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef constant(builder.emplace_instruction<ConstInstruction>(
            entry, Value::from_smi(7)));
        TaggedValueRef result(builder.emplace_instruction<AndSMIInstruction>(
            entry, parameter, constant));
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        EXPECT_EQ("graph {\n"
                  "bb0(%0):\n"
                  "  %1 = const {constant = 7}\n"
                  "  %2 = and_smi %0, %1\n"
                  "  return %2\n"
                  "}\n",
                  format_ir(*graph));
        EXPECT_EQ("%2 = and_smi %0, %1",
                  format_instruction(*graph, graph->storage()->instruction(
                                                 result.instruction_id())));
        EXPECT_EQ(format_ir(*graph), fmt::format("{}", *graph));
    }

    TEST(JitIRPrint, SegmentsSnapshotsAndNamesEdgeAttributes)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *join = builder.emplace_block();
        builder.set_loop_depth(join, 2);

        TaggedValueRef condition(builder.emplace_instruction<ConstInstruction>(
            entry, Value::True()));
        TaggedValueRef value(builder.emplace_instruction<ConstInstruction>(
            entry, Value::from_smi(1)));
        std::array<ProgramValueRef, 2> captured = {condition, value};
        SnapshotRef snapshot(builder.emplace_instruction<SnapshotInstruction>(
            entry, std::span<const ProgramValueRef>(captured), BytecodePC{7}));
        (void)snapshot;

        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(join));
        std::array<ProgramValueRef, 1> true_arguments = {value};
        std::array<ProgramValueRef, 1> false_arguments = {condition};
        BlockEdge *true_edge = builder.make_block_edge(
            entry, join, std::span<const ProgramValueRef>(true_arguments));
        BlockEdge *false_edge = builder.make_block_edge(
            entry, join, std::span<const ProgramValueRef>(false_arguments));
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, condition, true_edge, false_edge);
        builder.emplace_instruction<ReturnInstruction>(join, parameter);
        ControlFlowGraph *graph = builder.finalize();

        EXPECT_EQ("graph {\n"
                  "bb0:\n"
                  "  %0 = const {constant = true}\n"
                  "  %1 = const {constant = 1}\n"
                  "  %2 = snapshot [%0, %1] {resume_pc = 7}\n"
                  "  cond_br %0 {true_edge = bb1(%1), false_edge = bb1(%0)}\n"
                  "\n"
                  "bb1(%3) {loop_depth = 2}:\n"
                  "  return %3\n"
                  "}\n",
                  format_ir(*graph));
    }

    TEST(JitIRPrint, PrintsVariadicCallsAndNonDefaultBlockParameterTypes)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();

        TaggedValueRef callable(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef argument(
            builder.emplace_parameter<ParameterInstruction>(entry));
        F64Ref f64(builder.emplace_parameter<ParameterF64Instruction>(entry));
        PointerRef pointer(
            builder.emplace_parameter<ParameterPointerInstruction>(entry));
        (void)pointer;

        std::array<ProgramValueRef, 2> captured = {callable, argument};
        SnapshotRef snapshot(builder.emplace_instruction<SnapshotInstruction>(
            entry, std::span<const ProgramValueRef>(captured), BytecodePC{9}));
        std::array<TaggedValueRef, 1> arguments = {argument};
        builder.emplace_instruction<PythonCallInstruction>(
            entry, callable, snapshot,
            std::span<const TaggedValueRef>(arguments), BytecodePC{11});
        F64Ref sum(
            builder.emplace_instruction<AddF64Instruction>(entry, f64, f64));
        TaggedValueRef boxed(
            builder.emplace_instruction<BoxF64Instruction>(entry, sum));
        builder.emplace_instruction<ReturnInstruction>(entry, boxed);
        ControlFlowGraph *graph = builder.finalize();

        EXPECT_EQ("graph {\n"
                  "bb0(%0, %1, %2: f64, %3: ptr):\n"
                  "  %4 = snapshot [%0, %1] {resume_pc = 9}\n"
                  "  %5 = python_call %0, %4, [%1] "
                  "{interpreter_return_pc = 11}\n"
                  "  %6 = add_f64 %2, %2\n"
                  "  %7 = box_f64 %6\n"
                  "  return %7\n"
                  "}\n",
                  format_ir(*graph));
    }

    TEST(JitIRPrint, PrintsTheBytecodeStateOrderOnTheGraph)
    {
        test::VmTestContext context;
        CodeObject *code_object = context.compile_file(L"pass\n");
        code_object->function_signature.n_parameters = 0;
        code_object->n_locals = 2;
        code_object->n_temporaries = 1;

        CompilationSession session;
        GraphBuilder builder(session);
        builder.set_bytecode_state_order(BytecodeStateOrder(*code_object));
        Block *entry = builder.emplace_block();
        TaggedValueRef none(builder.emplace_instruction<ConstInstruction>(
            entry, Value::None()));
        builder.emplace_instruction<ReturnInstruction>(entry, none);
        ControlFlowGraph *graph = builder.finalize();

        EXPECT_EQ("graph state(acc, fp[3..-3]) {\n"
                  "bb0:\n"
                  "  %0 = const {constant = none}\n"
                  "  return %0\n"
                  "}\n",
                  format_ir(*graph));
    }

}  // namespace cl::jit
