#include "builtin_types/tuple.h"
#include "jit/aarch64_assembler.h"
#include "jit/aarch64_backend.h"
#include "jit/aarch64_cfg_emitter.h"
#include "jit/aarch64_jit_entry.h"
#include "jit/bytecode_state.h"
#include "jit/compilation_session.h"
#include "jit/core_bytecode_translator.h"
#include "jit/graph_builder.h"
#include "jit/jit_code_object.h"
#include "jit/jit_compiler.h"
#include "jit/jit_config.h"
#include "jit/location_assignments.h"
#include "jit/machine_address_internal.h"
#include "jit/register_allocator.h"
#include "object_model/function.h"
#include "runtime/runtime_helpers.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr PhysicalRegister x0(RegisterClass::GPR, 0);
        constexpr PhysicalRegister x1(RegisterClass::GPR, 1);
        constexpr PhysicalRegister x2(RegisterClass::GPR, 2);

        MachineAddress no_side_exit_thunk()
        {
            return detail::MachineAddressAccess::from_bits(0);
        }

        uint64_t inline_tag_guard_side_exit()
        {
            return static_cast<uint64_t>(Value::Ellipsis().as.integer);
        }

        uint32_t instruction_at(const void *code, size_t index)
        {
            uint32_t result;
            std::memcpy(&result,
                        static_cast<const uint8_t *>(code) +
                            index * sizeof(uint32_t),
                        sizeof(result));
            return result;
        }

        PublishedCode publish_allocation(CodeCache &cache,
                                         CodeAllocation &allocation,
                                         size_t tagged_value_count)
        {
            EXPECT_TRUE(cache.publish(allocation));
            return PublishedCode(allocation.code, allocation.constant_pool(),
                                 allocation.constant_pool_address(),
                                 tagged_value_count,
                                 allocation.encoded_code_size());
        }

        LocationAssignments
        assign_program_values_to_x0(const ControlFlowGraph &graph)
        {
            LocationAssignmentsBuilder locations;
            for(Block *block: graph.blocks())
            {
                for(Instruction parameter: block->parameters())
                {
                    locations.assign(ProgramValueRef(parameter),
                                     PhysicalLocation::reg(x0));
                }
                for(Instruction instruction: block->instructions())
                {
                    if(instruction.result_class() == ResultClass::ProgramValue)
                    {
                        locations.assign(ProgramValueRef(instruction),
                                         PhysicalLocation::reg(x0));
                    }
                }
            }
            return std::move(locations).finalize();
        }

        uint64_t
        execute_published_jit(const PublishedCode &code,
                              std::initializer_list<uint64_t> argument_bits)
        {
            std::array<Value, 128> frame;
            frame.fill(Value::None());
            Value *fp = frame.data() + frame.size() / 2;
            fp[FrameHeaderPreviousFpOffset] =
                encode_frame_payload_ptr(static_cast<Value *>(nullptr));

            uint32_t arity = static_cast<uint32_t>(argument_bits.size());
            uint32_t padded_arity = round_up_to_abi_alignment(arity);
            size_t index = 0;
            for(uint64_t bits: argument_bits)
            {
                int32_t frame_offset = int32_t(padded_arity) - 1 +
                                       FrameHeaderSizeAboveFp -
                                       int32_t(index++);
                fp[frame_offset].as.integer = static_cast<int64_t>(bits);
            }

            AArch64StandaloneJitEntryThunk thunk =
                reinterpret_cast<AArch64StandaloneJitEntryThunk>(
                    select_aarch64_standalone_jit_entry_thunk(arity)
                        .bits_for_indirect_target());
            Value result = thunk(Value::not_present(), fp, nullptr,
                                 reinterpret_cast<void *>(
                                     code.entry().bits_for_indirect_target()),
                                 nullptr, nullptr);
            return static_cast<uint64_t>(result.as.integer);
        }

        class PythonJitExecutionFixture
        {
        public:
            PythonJitExecutionFixture() : activation_scope_(context_.thread())
            {
            }

            void execute_module(const wchar_t *source)
            {
                CodeObject *code = context_.compile_file(source);
                module_ = code->get_defining_module().extract();
                (void)context_.thread()->run_clovervm_code_object(code);
            }

            Result<void, JitCompilationError>
            jit_compile(const wchar_t *function_name,
                        const JitCompilerOptions &options = {})
            {
                TValue<Function> target = function(function_name);
                CodeObject *code = target.extract()->code_object.extract();
                auto compilation =
                    compile_jit_code(*context_.thread(), *code, options);
                if(!compilation)
                {
                    return Result<void, JitCompilationError>::error(
                        std::move(compilation).error());
                }
                code->publish_jit_code(std::move(compilation).value());
                return Result<void, JitCompilationError>::ok();
            }

            template <typename... Args>
            Value call(const wchar_t *function_name, Args... args)
            {
                return context_.thread()->call_clovervm_function(
                    function(function_name), args...);
            }

            bool is_jit_compiled(const wchar_t *function_name) const
            {
                return function(function_name)
                    .extract()
                    ->code_object.extract()
                    ->has_jit_code();
            }

        private:
            TValue<Function> function(const wchar_t *name) const
            {
                assert(module_ != nullptr);
                Value value = module_->get_own_property(interned_string(name));
                assert(can_convert_to<Function>(value));
                return TValue<Function>::from_oop(
                    assume_convert_to<Function>(value));
            }

            test::VmTestContext context_;
            ThreadState::ActivationScope activation_scope_;
            ModuleObject *module_ = nullptr;
        };

    }  // namespace

    TEST(AArch64Execution, OmitsUnconditionalBranchToFallthroughBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        Block *target = builder.emplace_block();
        ParameterInstruction input =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(input)};
        BlockEdge *edge = builder.make_block_edge(entry, target, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction result =
            builder.emplace_parameter<ParameterInstruction>(target);
        builder.emplace_instruction<BareReturnInstruction>(
            target, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();
        LocationAssignments locations = assign_program_values_to_x0(*graph);

        CodeCache cache;
        auto emission = emit_aarch64_from_cfg(*graph, locations, cache,
                                              no_side_exit_thunk());

        ASSERT_TRUE(emission);
        PublishedCode code = std::move(emission).value();
        EXPECT_EQ(sizeof(uint32_t), code.encoded_code_size());
        constexpr uint64_t input_bits = 0x123456789abcdef0;
        EXPECT_EQ(input_bits, execute_published_jit(code, {input_bits}));
    }

    TEST(AArch64Execution, BranchesOnInlineTruthinessWithEitherFallthrough)
    {
        auto execute = [](bool true_falls_through, Value condition) {
            CompilationSession session;
            GraphBuilder builder(session, IRLevel::Machine);
            Block *entry = builder.emplace_block();
            Block *first = builder.emplace_block();
            Block *second = builder.emplace_block();
            Block *if_true = true_falls_through ? first : second;
            Block *if_false = true_falls_through ? second : first;
            ParameterInstruction input =
                builder.emplace_parameter<ParameterInstruction>(entry);
            BlockEdge *true_edge = builder.make_block_edge(entry, if_true);
            BlockEdge *false_edge = builder.make_block_edge(entry, if_false);
            builder.emplace_instruction<ConditionalBranchInstruction>(
                entry, TaggedValueRef(input), true_edge, false_edge);
            ConstInstruction true_result =
                builder.emplace_instruction<ConstInstruction>(if_true,
                                                              Value::True());
            builder.emplace_instruction<BareReturnInstruction>(
                if_true, TaggedValueRef(true_result));
            ConstInstruction false_result =
                builder.emplace_instruction<ConstInstruction>(if_false,
                                                              Value::False());
            builder.emplace_instruction<BareReturnInstruction>(
                if_false, TaggedValueRef(false_result));
            ControlFlowGraph *graph = builder.finalize();
            LocationAssignments locations = assign_program_values_to_x0(*graph);

            CodeCache cache;
            auto emission = emit_aarch64_from_cfg(*graph, locations, cache,
                                                  no_side_exit_thunk());
            EXPECT_TRUE(emission);
            PublishedCode code = std::move(emission).value();
            return execute_published_jit(
                code, {static_cast<uint64_t>(condition.as.integer)});
        };

        const Value falsy[] = {Value::None(), Value::False(),
                               Value::from_smi(0)};
        const Value truthy[] = {Value::True(), Value::Ellipsis(),
                                Value::from_smi(42)};
        for(bool true_falls_through: {false, true})
        {
            for(Value condition: falsy)
            {
                EXPECT_EQ(static_cast<uint64_t>(Value::False().as.integer),
                          execute(true_falls_through, condition));
            }
            for(Value condition: truthy)
            {
                EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer),
                          execute(true_falls_through, condition));
            }
        }
    }

    TEST(AArch64Execution, BranchesWhenNeitherSuccessorFallsThrough)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        Block *join = builder.emplace_block();
        Block *if_true = builder.emplace_block();
        Block *if_false = builder.emplace_block();
        ParameterInstruction condition =
            builder.emplace_parameter<ParameterInstruction>(entry);
        BlockEdge *true_edge = builder.make_block_edge(entry, if_true);
        BlockEdge *false_edge = builder.make_block_edge(entry, if_false);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(condition), true_edge, false_edge);
        ParameterInstruction result =
            builder.emplace_parameter<ParameterInstruction>(join);
        builder.emplace_instruction<BareReturnInstruction>(
            join, TaggedValueRef(result));
        ConstInstruction true_result =
            builder.emplace_instruction<ConstInstruction>(if_true,
                                                          Value::True());
        std::array<ProgramValueRef, 1> true_arguments = {
            ProgramValueRef(true_result)};
        BlockEdge *true_join =
            builder.make_block_edge(if_true, join, true_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(if_true,
                                                                    true_join);
        ConstInstruction false_result =
            builder.emplace_instruction<ConstInstruction>(if_false,
                                                          Value::False());
        std::array<ProgramValueRef, 1> false_arguments = {
            ProgramValueRef(false_result)};
        BlockEdge *false_join =
            builder.make_block_edge(if_false, join, false_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(if_false,
                                                                    false_join);
        ControlFlowGraph *graph = builder.finalize();
        LocationAssignments locations = assign_program_values_to_x0(*graph);

        CodeCache cache;
        auto emission = emit_aarch64_from_cfg(*graph, locations, cache,
                                              no_side_exit_thunk());

        ASSERT_TRUE(emission);
        PublishedCode code = std::move(emission).value();
        EXPECT_EQ(static_cast<uint64_t>(Value::False().as.integer),
                  execute_published_jit(
                      code, {static_cast<uint64_t>(Value::None().as.integer)}));
        EXPECT_EQ(
            static_cast<uint64_t>(Value::True().as.integer),
            execute_published_jit(
                code, {static_cast<uint64_t>(Value::from_smi(42).as.integer)}));
    }

    TEST(AArch64Execution, EmitsAllocationCreatedEdgeTransferBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        Block *target = builder.emplace_block();
        ParameterInstruction input =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(input)};
        BlockEdge *edge = builder.make_block_edge(entry, target, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction target_input =
            builder.emplace_parameter<ParameterInstruction>(target);
        MovInstruction result = builder.emplace_instruction<MovInstruction>(
            target, TaggedValueRef(target_input));
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                target, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        constexpr PhysicalRegister x16(RegisterClass::GPR, 16);
        constexpr PhysicalRegister x17(RegisterClass::GPR, 17);
        constexpr std::array registers = {x0, x1};
        constexpr std::array scratch = {x16, x17};
        auto fixed = [](PhysicalRegister reg) {
            return LocationRequirement::fixed(PhysicalLocation::reg(reg));
        };
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(input, std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(target_input,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x1)});
        overrides.emplace_back(result, std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        std::vector<RegisterClassDefinition> classes;
        classes.emplace_back(RegisterClass::GPR, registers, scratch);
        AllocationConstraints constraints(std::move(classes),
                                          std::move(overrides));

        auto locations = allocate_registers(session, *graph, constraints);
        ASSERT_TRUE(locations);
        ASSERT_EQ(3u, graph->blocks().size());
        ASSERT_EQ(InstructionKind::Mov,
                  graph->blocks()[1]->instruction_at(0).kind());

        CodeCache cache;
        auto emission = emit_aarch64_from_cfg(*graph, locations.value(), cache,
                                              no_side_exit_thunk());

        ASSERT_TRUE(emission);
        PublishedCode code = std::move(emission).value();
        constexpr uint64_t input_bits = 0x123456789abcdef0;
        EXPECT_EQ(input_bits, execute_published_jit(code, {input_bits}));
    }

    TEST(AArch64Execution, ExecutesInlineTagGuardWithColdSideExit)
    {
        test::VmTestContext vm;
        ThreadState::ActivationScope activation_scope(vm.thread());
        CodeObject *code_object = vm.compile_file(L"pass\n");
        code_object->function_signature.n_parameters = 1;
        BytecodeStateOrder state_order(*code_object);

        auto execute_guard = [&](InlineValueClass expected_class, Value input) {
            CompilationSession session;
            GraphBuilder builder(session, IRLevel::Machine);
            builder.set_bytecode_state_order(state_order);
            Block *entry = builder.emplace_block();
            ParameterInstruction parameter =
                builder.emplace_parameter<ParameterInstruction>(entry);
            std::array<ProgramValueRef, 1> inputs = {
                ProgramValueRef(parameter)};
            ParameterInstruction region_parameter =
                builder.make_instruction<ParameterInstruction>();
            std::vector<ProgramValueRef> region_captured(
                state_order.size(), ProgramValueRef(region_parameter));
            ExitToInterpreterInstruction region_exit =
                builder.make_instruction<ExitToInterpreterInstruction>(
                    region_captured, BytecodePCOffset{13});
            std::array<InstructionId, 1> parameter_ids = {
                region_parameter.id()};
            std::array<InstructionId, 1> instructions = {region_exit.id()};
            SideExitRegionId side_exit_region =
                builder.make_side_exit_region(parameter_ids, instructions)
                    ->id();
            InlineTagGuardWithSideExitInstruction guard =
                builder
                    .emplace_instruction<InlineTagGuardWithSideExitInstruction>(
                        entry, TaggedValueRef(parameter), inputs,
                        expected_class, side_exit_region);
            MovInstruction move = builder.emplace_instruction<MovInstruction>(
                entry, TaggedValueRef(guard));
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(move));
            ControlFlowGraph *graph = builder.finalize();

            LocationAssignmentsBuilder assignment_builder;
            assignment_builder.assign(ProgramValueRef(parameter),
                                      PhysicalLocation::reg(x0));
            assignment_builder.assign(ProgramValueRef(guard),
                                      PhysicalLocation::reg(x0));
            assignment_builder.assign(ProgramValueRef(move),
                                      PhysicalLocation::reg(x0));
            LocationAssignments locations =
                std::move(assignment_builder).finalize();

            CodeCache cache;
            MachineAddress side_exit_thunk =
                detail::MachineAddressAccess::from_pointer(
                    reinterpret_cast<const void *>(
                        &inline_tag_guard_side_exit));
            auto emission = emit_aarch64_from_cfg(*graph, locations, cache,
                                                  side_exit_thunk);
            EXPECT_TRUE(emission);
            PublishedCode code = std::move(emission).value();

            return execute_published_jit(
                code, {static_cast<uint64_t>(input.as.integer)});
        };

        Value smi = Value::from_smi(42);
        EXPECT_EQ(static_cast<uint64_t>(smi.as.integer),
                  execute_guard(InlineValueClass::SMI, smi));
        EXPECT_EQ(static_cast<uint64_t>(Value::Ellipsis().as.integer),
                  execute_guard(InlineValueClass::SMI, Value::True()));
        EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer),
                  execute_guard(InlineValueClass::Boolean, Value::True()));
        EXPECT_EQ(static_cast<uint64_t>(Value::Ellipsis().as.integer),
                  execute_guard(InlineValueClass::Boolean, smi));
        EXPECT_EQ(static_cast<uint64_t>(smi.as.integer),
                  execute_guard(InlineValueClass::SMIOrBoolean, smi));
        EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer),
                  execute_guard(InlineValueClass::SMIOrBoolean, Value::True()));
        EXPECT_EQ(static_cast<uint64_t>(Value::Ellipsis().as.integer),
                  execute_guard(InlineValueClass::SMIOrBoolean, Value::None()));
    }

    TEST(AArch64Execution, CombinesAdjacentInlineTagGuardsWithSameSideExit)
    {
        test::VmTestContext vm;
        ThreadState::ActivationScope activation_scope(vm.thread());
        CodeObject *code_object = vm.compile_file(L"pass\n");
        code_object->function_signature.n_parameters = 2;
        BytecodeStateOrder state_order(*code_object);
        CodeCache cache;

        auto make_guarded_function = [&](InlineValueClass expected_class) {
            CompilationSession session;
            GraphBuilder builder(session, IRLevel::Machine);
            builder.set_bytecode_state_order(state_order);
            Block *entry = builder.emplace_block();
            ParameterInstruction lhs =
                builder.emplace_parameter<ParameterInstruction>(entry);
            ParameterInstruction rhs =
                builder.emplace_parameter<ParameterInstruction>(entry);
            std::array<ProgramValueRef, 1> side_exit_arguments = {
                ProgramValueRef(lhs)};
            ParameterInstruction region_parameter =
                builder.make_instruction<ParameterInstruction>();
            std::vector<ProgramValueRef> region_captured(
                state_order.size(), ProgramValueRef(region_parameter));
            ExitToInterpreterInstruction region_exit =
                builder.make_instruction<ExitToInterpreterInstruction>(
                    region_captured, BytecodePCOffset{19});
            std::array<InstructionId, 1> parameter_ids = {
                region_parameter.id()};
            std::array<InstructionId, 1> instructions = {region_exit.id()};
            SideExitRegionId side_exit_region =
                builder.make_side_exit_region(parameter_ids, instructions)
                    ->id();
            InlineTagGuardWithSideExitInstruction lhs_guard =
                builder
                    .emplace_instruction<InlineTagGuardWithSideExitInstruction>(
                        entry, TaggedValueRef(lhs), side_exit_arguments,
                        expected_class, side_exit_region);
            InlineTagGuardWithSideExitInstruction rhs_guard =
                builder
                    .emplace_instruction<InlineTagGuardWithSideExitInstruction>(
                        entry, TaggedValueRef(rhs), side_exit_arguments,
                        expected_class, side_exit_region);
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(lhs_guard));
            ControlFlowGraph *graph = builder.finalize();

            LocationAssignmentsBuilder assignment_builder;
            assignment_builder.assign(ProgramValueRef(lhs),
                                      PhysicalLocation::reg(x0));
            assignment_builder.assign(ProgramValueRef(rhs),
                                      PhysicalLocation::reg(x1));
            assignment_builder.assign(ProgramValueRef(lhs_guard),
                                      PhysicalLocation::reg(x0));
            assignment_builder.assign(ProgramValueRef(rhs_guard),
                                      PhysicalLocation::reg(x1));
            LocationAssignments locations =
                std::move(assignment_builder).finalize();

            MachineAddress side_exit_thunk =
                detail::MachineAddressAccess::from_pointer(
                    reinterpret_cast<const void *>(
                        &inline_tag_guard_side_exit));
            auto emission = emit_aarch64_from_cfg(*graph, locations, cache,
                                                  side_exit_thunk);
            EXPECT_TRUE(emission);
            return std::move(emission).value();
        };

        auto execute_pair = [&](InlineValueClass expected_class, Value lhs,
                                Value rhs) {
            PublishedCode code = make_guarded_function(expected_class);
            size_t conditional_branch_count = 0;
            const void *instructions = reinterpret_cast<const void *>(
                code.entry().bits_for_indirect_target());
            size_t instruction_count =
                code.encoded_code_size() / sizeof(uint32_t);
            for(size_t index = 0; index < instruction_count; ++index)
            {
                if((instruction_at(instructions, index) & 0xff000010u) ==
                   0x54000000u)
                {
                    ++conditional_branch_count;
                }
            }
            EXPECT_EQ(1u, conditional_branch_count);

            return execute_published_jit(
                code, {static_cast<uint64_t>(lhs.as.integer),
                       static_cast<uint64_t>(rhs.as.integer)});
        };

        Value smi = Value::from_smi(42);
        EXPECT_EQ(static_cast<uint64_t>(smi.as.integer),
                  execute_pair(InlineValueClass::SMI, smi, smi));
        EXPECT_EQ(static_cast<uint64_t>(Value::Ellipsis().as.integer),
                  execute_pair(InlineValueClass::SMI, Value::True(), smi));
        EXPECT_EQ(static_cast<uint64_t>(Value::Ellipsis().as.integer),
                  execute_pair(InlineValueClass::SMI, smi, Value::True()));

        EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer),
                  execute_pair(InlineValueClass::Boolean, Value::True(),
                               Value::False()));
        EXPECT_EQ(static_cast<uint64_t>(Value::Ellipsis().as.integer),
                  execute_pair(InlineValueClass::Boolean, smi, Value::False()));
        EXPECT_EQ(static_cast<uint64_t>(Value::Ellipsis().as.integer),
                  execute_pair(InlineValueClass::Boolean, Value::False(), smi));

        EXPECT_EQ(
            static_cast<uint64_t>(smi.as.integer),
            execute_pair(InlineValueClass::SMIOrBoolean, smi, Value::True()));
        EXPECT_EQ(
            static_cast<uint64_t>(Value::Ellipsis().as.integer),
            execute_pair(InlineValueClass::SMIOrBoolean, Value::None(), smi));
        EXPECT_EQ(
            static_cast<uint64_t>(Value::Ellipsis().as.integer),
            execute_pair(InlineValueClass::SMIOrBoolean, smi, Value::None()));
    }

    TEST(AArch64Execution, ExecutesAddSMIWithColdOverflowSideExit)
    {
        test::VmTestContext vm;
        ThreadState::ActivationScope activation_scope(vm.thread());
        CodeObject *code_object = vm.compile_file(L"pass\n");
        code_object->function_signature.n_parameters = 2;
        BytecodeStateOrder state_order(*code_object);

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        builder.set_bytecode_state_order(state_order);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> inputs = {ProgramValueRef(lhs)};
        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        std::vector<ProgramValueRef> region_captured(
            state_order.size(), ProgramValueRef(region_parameter));
        ExitToInterpreterInstruction region_exit =
            builder.make_instruction<ExitToInterpreterInstruction>(
                region_captured, BytecodePCOffset{17});
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {region_exit.id()};
        SideExitRegionId side_exit_region =
            builder.make_side_exit_region(parameter_ids, instructions)->id();
        AddSMIWithSideExitInstruction add =
            builder.emplace_instruction<AddSMIWithSideExitInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs), inputs,
                side_exit_region);
        MovInstruction move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(add));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(move));
        ControlFlowGraph *graph = builder.finalize();

        LocationAssignmentsBuilder assignment_builder;
        assignment_builder.assign(ProgramValueRef(lhs),
                                  PhysicalLocation::reg(x0));
        assignment_builder.assign(ProgramValueRef(rhs),
                                  PhysicalLocation::reg(x1));
        assignment_builder.assign(ProgramValueRef(add),
                                  PhysicalLocation::reg(x2));
        assignment_builder.assign(ProgramValueRef(move),
                                  PhysicalLocation::reg(x0));
        LocationAssignments locations =
            std::move(assignment_builder).finalize();

        CodeCache cache;
        MachineAddress side_exit_thunk =
            detail::MachineAddressAccess::from_pointer(
                reinterpret_cast<const void *>(&inline_tag_guard_side_exit));
        auto emission =
            emit_aarch64_from_cfg(*graph, locations, cache, side_exit_thunk);
        ASSERT_TRUE(emission);
        PublishedCode code = std::move(emission).value();

        EXPECT_EQ(
            static_cast<uint64_t>(Value::from_smi(42).as.integer),
            execute_published_jit(
                code, {static_cast<uint64_t>(Value::from_smi(19).as.integer),
                       static_cast<uint64_t>(Value::from_smi(23).as.integer)}));
        EXPECT_EQ(
            static_cast<uint64_t>(Value::Ellipsis().as.integer),
            execute_published_jit(
                code, {static_cast<uint64_t>(
                           Value::from_smi(value_smi_max).as.integer),
                       static_cast<uint64_t>(Value::from_smi(1).as.integer)}));
    }

    TEST(AArch64Execution, CompilesPythonIdentityFunction)
    {
        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def identity(value):\n"
                               L"    copy = value\n"
                               L"    return copy\n");
        ASSERT_TRUE(fixture.jit_compile(L"identity"));

        const Value inputs[] = {Value::None(), Value::True(),
                                Value::from_smi(42)};
        for(Value input: inputs)
        {
            EXPECT_EQ(input, fixture.call(L"identity", input));
        }
    }

    TEST(AArch64Execution, InterpreterCallsPublishedTierAfterArgumentAdaptation)
    {
        if constexpr(!JitTieringEnabled)
        {
            GTEST_SKIP() << "runtime JIT tiering is disabled";
        }

        PythonJitExecutionFixture fixture;
        std::wstring source = L"def collect(*args, **kwargs):\n"
                              L"    return args\n"
                              L"for index in range(" +
                              std::to_wstring(InitialJitTieringBudget) +
                              L"):\n"
                              L"    collect(value=index)\n";
        fixture.execute_module(source.c_str());
        Value result = fixture.call(L"collect", Value::from_smi(42));
        ASSERT_TRUE(can_convert_to<Tuple>(result));
        Tuple *arguments = assume_convert_to<Tuple>(result);
        ASSERT_EQ(1u, arguments->size());
        EXPECT_EQ(Value::from_smi(42), arguments->item_unchecked(0));
        EXPECT_TRUE(fixture.is_jit_compiled(L"collect"));
    }

    TEST(AArch64Execution, CompilesBytecodeThroughJitCompiler)
    {
        class Observer : public JitCompilationObserver
        {
        public:
            void on_bytecode(const CodeObject &) override
            {
                saw_bytecode = true;
            }

            void on_core_ir_translated(const ControlFlowGraph &graph) override
            {
                translated_instruction_count =
                    graph.blocks().front()->instructions().size();
            }

            void on_core_ir_optimized(const ControlFlowGraph &graph) override
            {
                optimized_instruction_count =
                    graph.blocks().front()->instructions().size();
            }

            void on_machine_code(const PublishedCode &) override
            {
                saw_machine_code = true;
            }

            bool saw_bytecode = false;
            bool saw_machine_code = false;
            size_t translated_instruction_count = 0;
            size_t optimized_instruction_count = 0;
        };

        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def identity(value):\n"
                               L"    unused = None\n"
                               L"    return value\n");
        Observer observer;
        ASSERT_TRUE(
            fixture.jit_compile(L"identity", JitCompilerOptions{&observer}));
        EXPECT_TRUE(observer.saw_bytecode);
        EXPECT_TRUE(observer.saw_machine_code);
        EXPECT_GT(observer.translated_instruction_count,
                  observer.optimized_instruction_count);

        Value input = Value::from_smi(42);
        EXPECT_EQ(input, fixture.call(L"identity", input));
    }

    TEST(AArch64Execution, CompilesPythonConstantFunction)
    {
        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def answer():\n"
                               L"    return 42\n");
        ASSERT_TRUE(fixture.jit_compile(L"answer"));
        EXPECT_EQ(Value::from_smi(42), fixture.call(L"answer"));
    }

    TEST(AArch64Execution, CompilesPythonFunctionReturningSecondArgument)
    {
        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def second(first, second):\n"
                               L"    return second\n");
        ASSERT_TRUE(fixture.jit_compile(L"second"));

        Value first = Value::from_smi(19);
        Value second = Value::from_smi(23);
        EXPECT_EQ(second, fixture.call(L"second", first, second));
    }

    TEST(AArch64Execution, CompilesPythonIs)
    {
        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def is_same(lhs, rhs):\n"
                               L"    return lhs is rhs\n");
        ASSERT_TRUE(fixture.jit_compile(L"is_same"));

        EXPECT_EQ(Value::True(),
                  fixture.call(L"is_same", Value::None(), Value::None()));
        EXPECT_EQ(Value::False(),
                  fixture.call(L"is_same", Value::None(), Value::True()));
    }

    TEST(AArch64Execution, CompilesPythonIsNot)
    {
        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def is_distinct(lhs, rhs):\n"
                               L"    return lhs is not rhs\n");
        ASSERT_TRUE(fixture.jit_compile(L"is_distinct"));

        EXPECT_EQ(Value::False(),
                  fixture.call(L"is_distinct", Value::None(), Value::None()));
        EXPECT_EQ(Value::True(),
                  fixture.call(L"is_distinct", Value::None(), Value::True()));
    }

    TEST(AArch64Execution, CompilesGuardedSMIAdditionWithoutGuardCopies)
    {
        class Observer : public JitCompilationObserver
        {
        public:
            void on_machine_ir(const ControlFlowGraph &graph) override
            {
                for(const Block *block: graph.blocks())
                {
                    for(Instruction instruction: block->instructions())
                    {
                        if(instruction.kind() == InstructionKind::Mov)
                        {
                            ++move_count;
                        }
                        else if(instruction.kind() ==
                                InstructionKind::InlineTagGuardWithSideExit)
                        {
                            ++inline_tag_guard_count;
                        }
                    }
                }
            }

            size_t move_count = 0;
            size_t inline_tag_guard_count = 0;
        };

        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def add(lhs, rhs):\n"
                               L"    return lhs + rhs\n");
        Observer observer;
        ASSERT_TRUE(fixture.jit_compile(L"add", JitCompilerOptions{&observer}));
        EXPECT_EQ(2u, observer.inline_tag_guard_count);
        EXPECT_EQ(1u, observer.move_count);

        EXPECT_EQ(Value::from_smi(42), fixture.call(L"add", Value::from_smi(19),
                                                    Value::from_smi(23)));
    }

    TEST(AArch64Execution, ResumesInterpreterForStringAdditionSideExit)
    {
        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def add(lhs, rhs):\n"
                               L"    return lhs + rhs\n");
        ASSERT_TRUE(fixture.jit_compile(L"add"));

        EXPECT_EQ(Value::from_smi(42), fixture.call(L"add", Value::from_smi(19),
                                                    Value::from_smi(23)));

        Owned<TValue<String>> lhs(string_value(L"hello "));
        Owned<TValue<String>> rhs(string_value(L"world"));
        Value result = fixture.call(L"add", lhs.value().raw_value(),
                                    rhs.value().raw_value());

        ASSERT_TRUE(can_convert_to<String>(result));
        EXPECT_STREQ(
            L"hello world",
            string_as_wchar_t(TValue<String>::from_value_assumed(result)));
    }

    TEST(AArch64Execution, CompilesPythonIdentityConditional)
    {
        class Observer : public JitCompilationObserver
        {
        public:
            void on_machine_ir(const ControlFlowGraph &graph) override
            {
                block_count = graph.blocks().size();
                for(const Block *block: graph.blocks())
                {
                    for(Instruction instruction: block->instructions())
                    {
                        switch(instruction.kind())
                        {
                            case InstructionKind::Mov:
                                ++move_count;
                                break;
                            case InstructionKind::UnconditionalBranch:
                                ++machine_unconditional_branch_count;
                                break;
                            default:
                                break;
                        }
                    }
                }
            }

            void on_machine_code(const PublishedCode &code) override
            {
                const void *instructions = reinterpret_cast<const void *>(
                    code.entry().bits_for_indirect_target());
                size_t instruction_count =
                    code.encoded_code_size() / sizeof(uint32_t);
                for(size_t index = 0; index < instruction_count; ++index)
                {
                    uint32_t instruction = instruction_at(instructions, index);
                    if((instruction & 0xfc000000u) == 0x14000000u)
                    {
                        ++emitted_unconditional_branch_count;
                    }
                }
            }

            size_t block_count = 0;
            size_t move_count = 0;
            size_t machine_unconditional_branch_count = 0;
            size_t emitted_unconditional_branch_count = 0;
        };

        PythonJitExecutionFixture fixture;
        fixture.execute_module(L"def choose(a, b, c, d):\n"
                               L"    if a is b:\n"
                               L"        return c\n"
                               L"    return d\n");
        Observer observer;
        ASSERT_TRUE(
            fixture.jit_compile(L"choose", JitCompilerOptions{&observer}));
        EXPECT_EQ(5u, observer.block_count);
        EXPECT_EQ(2u, observer.move_count);
        EXPECT_EQ(2u, observer.machine_unconditional_branch_count);
        EXPECT_EQ(0u, observer.emitted_unconditional_branch_count);

        Value if_true = Value::from_smi(19);
        Value if_false = Value::from_smi(23);
        EXPECT_EQ(if_true, fixture.call(L"choose", Value::None(), Value::None(),
                                        if_true, if_false));
        EXPECT_EQ(if_false, fixture.call(L"choose", Value::None(),
                                         Value::True(), if_true, if_false));
    }

    TEST(AArch64Execution, EntersJitThroughCachedArityThunk)
    {
        for(uint32_t arity = 0; arity <= 8; ++arity)
        {
            PythonJitExecutionFixture fixture;
            std::wstring source = L"def f(";
            for(uint32_t index = 0; index < arity; ++index)
            {
                if(index != 0)
                {
                    source += L", ";
                }
                source += L"p" + std::to_wstring(index);
            }
            source += L"): return ";
            source += arity == 0 ? L"17\n"
                                 : L"p" + std::to_wstring(arity - 1) + L"\n";
            source += L"def invoke(): return f(";
            Value expected = Value::from_smi(17);
            for(uint32_t index = 0; index < arity; ++index)
            {
                if(index != 0)
                {
                    source += L", ";
                }
                source += std::to_wstring(100 + index);
                expected = Value::from_smi(100 + index);
            }
            source += L")\n";

            fixture.execute_module(source.c_str());
            ASSERT_TRUE(fixture.jit_compile(L"f")) << "arity " << arity;
            EXPECT_EQ(expected, fixture.call(L"invoke")) << "arity " << arity;
        }
    }

    TEST(AArch64Execution, EmitsManagedFrameRelativeStackTransfers)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        LoadStackInstruction load =
            builder.emplace_instruction<LoadStackInstruction>(
                entry, TaggedValueRef(parameter));
        StoreStackInstruction store =
            builder.emplace_instruction<StoreStackInstruction>(
                entry, TaggedValueRef(load));
        LoadStackInstruction reload =
            builder.emplace_instruction<LoadStackInstruction>(
                entry, TaggedValueRef(store));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(reload));
        ControlFlowGraph *graph = builder.finalize();

        LocationAssignmentsBuilder location_builder;
        location_builder.assign(ProgramValueRef(parameter),
                                PhysicalLocation::stack(StackLocation(
                                    StackLocationKind::IncomingParameter, 4)));
        location_builder.assign(ProgramValueRef(load),
                                PhysicalLocation::reg(x1));
        location_builder.assign(ProgramValueRef(store),
                                PhysicalLocation::stack(StackLocation(
                                    StackLocationKind::LocalOrTemporary, -1)));
        location_builder.assign(ProgramValueRef(reload),
                                PhysicalLocation::reg(x0));
        LocationAssignments locations = std::move(location_builder).finalize();
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);

        generate_aarch64_assembly(*graph, locations, assembler,
                                  no_side_exit_thunk());
        CodeCache cache;
        Result<CodeAllocation, JitCodeError> finalization =
            assembler.emitter().finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();
        const void *code = allocation.writable_code().data();
        EXPECT_EQ(0xf94012a1, instruction_at(code, 0));
        EXPECT_EQ(0xf81f82a1, instruction_at(code, 1));
        EXPECT_EQ(0xf85f82a0, instruction_at(code, 2));
        EXPECT_EQ(0xd65f03c0, instruction_at(code, 3));
    }

    TEST(AArch64Execution, CallsGeneratedLeafFunction)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();

        assembler.emit_arithmetic_reg(ArithmeticOp::Add, XRegister(0),
                                      XRegister(0), XRegister(1));
        assembler.emit_ret();

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        PublishedCode code =
            publish_allocation(cache, allocation, emitter.tagged_value_count());

        using Function = int64_t (*)(int64_t, int64_t);
        Function function =
            reinterpret_cast<Function>(code.entry().bits_for_indirect_target());
        EXPECT_EQ(42, function(19, 23));
    }

    TEST(AArch64Execution, CallsGeneratedFunctionWithBranches)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();
        Label done = emitter.make_label();

        assembler.cmp(XRegister(0), XRegister(1));
        assembler.emit_b_conditional_immediate(
            AArch64Condition::SignedGreaterOrEqual, 12);
        assembler.emit_arithmetic_reg(ArithmeticOp::Sub, XRegister(0),
                                      XRegister(1), XRegister(0));
        assembler.b(done);
        assembler.emit_arithmetic_reg(ArithmeticOp::Sub, XRegister(0),
                                      XRegister(0), XRegister(1));
        emitter.resolve(done);
        assembler.emit_ret();

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        PublishedCode code =
            publish_allocation(cache, allocation, emitter.tagged_value_count());

        using Function = int64_t (*)(int64_t, int64_t);
        Function function =
            reinterpret_cast<Function>(code.entry().bits_for_indirect_target());
        EXPECT_EQ(5, function(9, 4));
        EXPECT_EQ(5, function(4, 9));
        EXPECT_EQ(0, function(7, 7));
    }

    TEST(AArch64Execution, LoadsAndRewritesValueFromPreferredConstantPool)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.ldr(XRegister(0), Value::True());
        assembler.emit_ret();

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        PublishedCode code =
            publish_allocation(cache, allocation, emitter.tagged_value_count());

        using Function = uint64_t (*)();
        Function function =
            reinterpret_cast<Function>(code.entry().bits_for_indirect_target());
        EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer), function());

        code.tagged_values()[0] = Value::False();
        EXPECT_EQ(static_cast<uint64_t>(Value::False().as.integer), function());
    }

    TEST(AArch64Execution, LoadsAndRewritesValueFromFarConstantPool)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::FarPageRelative);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.ldr(XRegister(0), Value::True());
        assembler.emit_ret();

        constexpr size_t PaddingSize = 2 * 1024 * 1024;
        for(size_t emitted = 0; emitted < PaddingSize;
            emitted += sizeof(uint32_t))
        {
            assembler.emit_ret();
        }

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        PublishedCode code =
            publish_allocation(cache, allocation, emitter.tagged_value_count());

        using Function = uint64_t (*)();
        Function function =
            reinterpret_cast<Function>(code.entry().bits_for_indirect_target());
        EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer), function());

        code.tagged_values()[0] = Value::False();
        EXPECT_EQ(static_cast<uint64_t>(Value::False().as.integer), function());
    }
}  // namespace cl::jit
