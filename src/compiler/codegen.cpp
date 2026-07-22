#include "compiler/codegen.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_builder.h"
#include "compiler/ast.h"
#include "compiler/codegen_scope_analysis.h"
#include "compiler/scope.h"
#include "compiler/tokenizer.h"
#include "object_model/attr.h"
#include "object_model/typed_value.h"
#include "runtime/runtime_helpers.h"
#include <fmt/core.h>
#include <optional>
#include <utility>

namespace cl
{

    struct OpTableEntry
    {
        constexpr OpTableEntry()
            : standard(Bytecode::Invalid), binary_acc_smi(Bytecode::Invalid),
              bytecode_format(OperatorBytecodeFormat::Plain)
        {
        }

        constexpr OpTableEntry(OperatorBytecodeFormat _bytecode_format,
                               Bytecode _standard,
                               Bytecode _binary_acc_smi = Bytecode::Invalid)
            : standard(_standard), binary_acc_smi(_binary_acc_smi),
              bytecode_format(_bytecode_format)
        {
        }

        Bytecode standard;
        Bytecode binary_acc_smi;
        OperatorBytecodeFormat bytecode_format;
    };

    struct OpTable
    {
        OpTableEntry table[AstOperatorKindSize];
    };

    constexpr OpTable make_table()
    {
        OpTable t;

        t.table[size_t(AstOperatorKind::ADD)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::Add, Bytecode::AddSmi);
        t.table[size_t(AstOperatorKind::SUBTRACT)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::Sub, Bytecode::SubSmi);
        t.table[size_t(AstOperatorKind::MULTIPLY)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::Mul, Bytecode::MulSmi);
        t.table[size_t(AstOperatorKind::MATMULT)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::MatMul);
        t.table[size_t(AstOperatorKind::DIVIDE)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::TrueDiv);
        t.table[size_t(AstOperatorKind::INT_DIVIDE)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::FloorDiv, Bytecode::FloorDivSmi);
        t.table[size_t(AstOperatorKind::POWER)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::BinaryPow, Bytecode::BinaryPowSmi);
        t.table[size_t(AstOperatorKind::LEFTSHIFT)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::LShift, Bytecode::LShiftSmi);
        t.table[size_t(AstOperatorKind::RIGHTSHIFT)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::RShift, Bytecode::RShiftSmi);
        t.table[size_t(AstOperatorKind::MODULO)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::Mod, Bytecode::ModSmi);
        t.table[size_t(AstOperatorKind::BITWISE_OR)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::Or, Bytecode::OrSmi);
        t.table[size_t(AstOperatorKind::BITWISE_AND)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::And, Bytecode::AndSmi);
        t.table[size_t(AstOperatorKind::BITWISE_XOR)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::Xor, Bytecode::XorSmi);

        t.table[size_t(AstOperatorKind::EQUAL)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::TestEqual);
        t.table[size_t(AstOperatorKind::NOT_EQUAL)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::TestNotEqual);
        t.table[size_t(AstOperatorKind::LESS)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::TestLess);
        t.table[size_t(AstOperatorKind::LESS_EQUAL)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::TestLessEqual);
        t.table[size_t(AstOperatorKind::GREATER)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::TestGreater);
        t.table[size_t(AstOperatorKind::GREATER_EQUAL)] = OpTableEntry(
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck,
            Bytecode::TestGreaterEqual);
        t.table[size_t(AstOperatorKind::IS)] =
            OpTableEntry(OperatorBytecodeFormat::Plain, Bytecode::TestIs);
        t.table[size_t(AstOperatorKind::IS_NOT)] =
            OpTableEntry(OperatorBytecodeFormat::Plain, Bytecode::TestIsNot);
        t.table[size_t(AstOperatorKind::IN)] =
            OpTableEntry(OperatorBytecodeFormat::WithCache, Bytecode::Contains);
        t.table[size_t(AstOperatorKind::NOT_IN)] =
            OpTableEntry(OperatorBytecodeFormat::WithCache, Bytecode::Contains);

        t.table[size_t(AstOperatorKind::NOT)] =
            OpTableEntry(OperatorBytecodeFormat::Plain, Bytecode::Not);
        t.table[size_t(AstOperatorKind::NEGATE)] =
            OpTableEntry(OperatorBytecodeFormat::WithCache, Bytecode::Neg);
        t.table[size_t(AstOperatorKind::PLUS)] =
            OpTableEntry(OperatorBytecodeFormat::WithCache, Bytecode::Pos);
        t.table[size_t(AstOperatorKind::BITWISE_NOT)] =
            OpTableEntry(OperatorBytecodeFormat::WithCache, Bytecode::Invert);

        return t;
    }

    Expected<CodeObject *> codegen_function(const AstVector &av,
                                            ModuleObject *module,
                                            CodeObjectBuilder *parent_code_obj,
                                            int32_t node_idx,
                                            LanguageMode language_mode);
    Expected<CodeObject *> codegen_class(const AstVector &av,
                                         ModuleObject *module,
                                         CodeObjectBuilder *parent_code_obj,
                                         int32_t node_idx,
                                         LanguageMode language_mode);

    void reserve_parameter_padding_and_frame_header(
        CodeObjectBuilder *target_code_obj)
    {
        uint32_t n_parameter_padding =
            target_code_obj->get_padded_n_parameters() -
            target_code_obj->n_parameters();
        target_code_obj->get_local_scope_ptr()->reserve_empty_slots(
            n_parameter_padding);
        target_code_obj->get_local_scope_ptr()->reserve_empty_slots(
            FrameHeaderSize);
    }

    AstChildren parameter_signature_group(const AstVector &av,
                                          int32_t signature_idx,
                                          uint32_t group_idx)
    {
        assert(av.kinds[signature_idx].node_kind ==
               AstNodeKind::PARAMETER_SIGNATURE);
        assert(group_idx < av.children[signature_idx].size());
        return av.children[av.children[signature_idx][group_idx]];
    }

    Expected<AstChildren>
    supported_runtime_parameter_order(const AstVector &av,
                                      int32_t signature_idx)
    {
        AstChildren result = parameter_signature_group(av, signature_idx, 0);
        AstChildren pos_or_kw = parameter_signature_group(av, signature_idx, 1);
        for(int32_t param_idx: pos_or_kw)
        {
            result.push_back(param_idx);
        }
        AstChildren vararg = parameter_signature_group(av, signature_idx, 2);
        assert(vararg.size() <= 1);
        for(int32_t param_idx: vararg)
        {
            result.push_back(param_idx);
        }
        AstChildren kwonly = parameter_signature_group(av, signature_idx, 3);
        for(int32_t param_idx: kwonly)
        {
            result.push_back(param_idx);
        }
        AstChildren kwarg = parameter_signature_group(av, signature_idx, 4);
        assert(kwarg.size() <= 1);
        for(int32_t param_idx: kwarg)
        {
            result.push_back(param_idx);
        }
        return Expected<AstChildren>::ok(std::move(result));
    }

    TValue<String> ast_string_constant(const AstVector &av, int32_t node_idx)
    {
        return TValue<String>::from_value_assumed(av.constants[node_idx]);
    }

    class AstCodegen
    {
    public:
        using RegisterIndex = int32_t;

        static Expected<AstCodegen>
        make(const AstVector &av, CodeObjectBuilder *code_obj, CodegenMode mode,
             LanguageMode language_mode, int32_t body_idx,
             AstChildren param_children,
             ModuleResultMode result_mode = ModuleResultMode::File)
        {
            ScopeAnalysis analysis = CL_TRY(analyze_code_object_scope(
                av, code_obj, body_idx, mode, param_children));
            return Expected<AstCodegen>::ok(
                AstCodegen(av, code_obj, body_idx, std::move(analysis),
                           language_mode, result_mode));
        }

        Expected<CodeObject *> run_module();
        Expected<CodeObject *> run_function_body(uint32_t source_offset,
                                                 int32_t body_idx);
        Expected<CodeObject *> run_class_body(uint32_t source_offset,
                                              int32_t body_idx);

    private:
        AstCodegen(const AstVector &_av, CodeObjectBuilder *_code_obj,
                   int32_t _body_idx, ScopeAnalysis _analysis,
                   LanguageMode _language_mode, ModuleResultMode _result_mode)
            : av(_av), code_obj(_code_obj), body_idx(_body_idx),
              analysis(std::move(_analysis)), language_mode(_language_mode),
              result_mode(_result_mode)
        {
        }

        struct LoopTargetSet
        {
            LoopTargetSet(JumpTarget *_break_target,
                          JumpTarget *_continue_target, size_t _cleanup_depth)
                : break_target(_break_target),
                  continue_target(_continue_target),
                  cleanup_depth(_cleanup_depth)
            {
            }

            JumpTarget *break_target;
            JumpTarget *continue_target;
            size_t cleanup_depth;
        };

        struct CleanupContext
        {
            enum class Kind
            {
                FinallyBody,
                WithExit
            };

            static CleanupContext
            finally_body(int32_t body_idx,
                         ExceptionTableRangeBuilder *exception_range)
            {
                return CleanupContext{Kind::FinallyBody, body_idx, 0, 0,
                                      exception_range};
            }

            static CleanupContext
            with_exit(uint32_t source_offset, RegisterIndex manager_reg,
                      ExceptionTableRangeBuilder *exception_range)
            {
                return CleanupContext{Kind::WithExit, -1, source_offset,
                                      manager_reg, exception_range};
            }

            Kind kind;
            int32_t body_idx;
            uint32_t source_offset;
            RegisterIndex manager_reg;
            ExceptionTableRangeBuilder *exception_range;
        };

        constexpr static OpTable operator_table = make_table();

        constexpr static OpTableEntry get_operator_entry(AstOperatorKind ok)
        {
            return operator_table.table[size_t(ok)];
        }

        static bool is_membership_operator(AstOperatorKind ok)
        {
            return ok == AstOperatorKind::IN || ok == AstOperatorKind::NOT_IN;
        }

        std::optional<int8_t> check_binary_acc_smi_immediate(
            AstOperatorKind op_kind, OpTableEntry entry, int32_t rhs_idx) const
        {
            AstKind rhs_kind = av.kinds[rhs_idx];
            if(entry.binary_acc_smi == Bytecode::Invalid ||
               rhs_kind.node_kind != NumericalConstant.node_kind ||
               rhs_kind.operator_kind != NumericalConstant.operator_kind)
            {
                return std::nullopt;
            }

            Value rhs = av.constants[rhs_idx];
            if(!rhs.is_smi8())
            {
                return std::nullopt;
            }

            int8_t immediate = rhs.get_smi();
            if(op_kind == AstOperatorKind::LEFTSHIFT &&
               (immediate < 0 || immediate >= 64))
            {
                return std::nullopt;
            }

            return immediate;
        }

        const AstVector &av;
        CodegenMode mode() const { return analysis.mode; }

        CodeObjectBuilder *code_obj;
        int32_t body_idx;
        ScopeAnalysis analysis;
        LanguageMode language_mode;
        ModuleResultMode result_mode;
        std::vector<LoopTargetSet> loop_targets;
        std::vector<RegisterIndex> caught_exception_regs;
        std::vector<CleanupContext> active_cleanups;

        static bool handler_has_type(const AstChildren &handler_children)
        {
            return handler_children.size() >= 2;
        }

        static bool handler_has_name(const AstChildren &handler_children)
        {
            return handler_children.size() == 3;
        }

        static int32_t handler_type_idx(const AstChildren &handler_children)
        {
            assert(handler_has_type(handler_children));
            return handler_children[0];
        }

        static int32_t handler_name_idx(const AstChildren &handler_children)
        {
            assert(handler_has_name(handler_children));
            return handler_children[1];
        }

        static int32_t handler_body_idx(const AstChildren &handler_children)
        {
            return handler_children.back();
        }

        bool ann_assign_is_simple(int32_t node_idx) const
        {
            assert(av.kinds[node_idx].node_kind ==
                   AstNodeKind::STATEMENT_ANN_ASSIGN);
            return av.constants[node_idx] == Value::True();
        }

        const NameAccessAnalysis &load_access(int32_t node_idx) const
        {
            assert(analysis.loads[node_idx].has_value());
            return *analysis.loads[node_idx];
        }

        const NameAccessAnalysis &store_access(int32_t node_idx) const
        {
            assert(analysis.stores[node_idx].has_value());
            return *analysis.stores[node_idx];
        }

        const NameAccessAnalysis &delete_access(int32_t node_idx) const
        {
            assert(analysis.deletes[node_idx].has_value());
            return *analysis.deletes[node_idx];
        }

        Expected<void> emit_local_binding_prologue()
        {
            for(const BindingInfo &binding: analysis.bindings)
            {
                if(binding.scope == BindingScope::Local &&
                   binding.needs_entry_clear)
                {
                    CL_TRY(
                        code_obj->emit_clear_local(0, binding.local_slot_idx));
                }
            }
            return Expected<void>::ok();
        }

        Expected<void> emit_variable_load(uint32_t source_offset,
                                          int32_t node_idx)
        {
            const NameAccessAnalysis &access = load_access(node_idx);
            switch(access.scope)
            {
                case BindingScope::Local:
                    if(access.presence == Presence::Present)
                    {
                        CL_TRY(code_obj->emit_ldar(source_offset,
                                                   access.slot_idx));
                    }
                    else
                    {
                        CL_TRY(code_obj->emit_load_local_checked(
                            source_offset, access.slot_idx));
                    }
                    break;
                case BindingScope::Global:
                    {
                        uint8_t name_idx = CL_TRY(code_obj->allocate_constant(
                            av.constants[node_idx]));
                        CL_TRY(
                            code_obj->emit_lda_global(source_offset, name_idx));
                    }
                    break;
            }
            return Expected<void>::ok();
        }

        Expected<void> emit_variable_store(uint32_t source_offset,
                                           int32_t node_idx)
        {
            const NameAccessAnalysis &access = store_access(node_idx);
            switch(access.scope)
            {
                case BindingScope::Local:
                    CL_TRY(code_obj->emit_star(source_offset, access.slot_idx));
                    break;
                case BindingScope::Global:
                    {
                        uint8_t name_idx = CL_TRY(code_obj->allocate_constant(
                            av.constants[node_idx]));
                        CL_TRY(
                            code_obj->emit_sta_global(source_offset, name_idx));
                    }
                    break;
            }
            return Expected<void>::ok();
        }

        Expected<void> emit_variable_delete(uint32_t source_offset,
                                            int32_t node_idx)
        {
            const NameAccessAnalysis &access = delete_access(node_idx);
            switch(access.scope)
            {
                case BindingScope::Local:
                    CL_TRY(code_obj->emit_del_local(source_offset,
                                                    access.slot_idx));
                    break;
                case BindingScope::Global:
                    {
                        uint8_t name_idx = CL_TRY(code_obj->allocate_constant(
                            av.constants[node_idx]));
                        CL_TRY(
                            code_obj->emit_del_global(source_offset, name_idx));
                    }
                    break;
            }
            return Expected<void>::ok();
        }

        using TemporaryReg = CodeObjectBuilder::TemporaryReg;

        struct ScopedRegister
        {
            RegisterIndex reg;
            std::optional<TemporaryReg> temp;
        };

        struct ValueLocation
        {
            enum class Kind
            {
                Accumulator,
                Register
            };

            static ValueLocation accumulator()
            {
                return ValueLocation{Kind::Accumulator, 0};
            }

            static ValueLocation reg(RegisterIndex reg)
            {
                return ValueLocation{Kind::Register, reg};
            }

            bool is_register() const { return kind == Kind::Register; }

            Kind kind;
            RegisterIndex register_index;
        };

        static constexpr AstKind NumericalConstant =
            AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NUMBER);

        Expected<ValueLocation>
        codegen_node_into_value_location(int32_t node_idx)
        {
            std::optional<RegisterIndex> location =
                existing_register_location(node_idx);
            if(location.has_value())
            {
                return Expected<ValueLocation>::ok(
                    ValueLocation::reg(*location));
            }

            CL_TRY(codegen_node(node_idx));
            return Expected<ValueLocation>::ok(ValueLocation::accumulator());
        }

        std::optional<RegisterIndex>
        existing_register_location(int32_t node_idx) const
        {
            AstKind kind = av.kinds[node_idx];
            if(kind.node_kind != AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                return std::nullopt;
            }

            const NameAccessAnalysis &access = load_access(node_idx);
            if(access.scope != BindingScope::Local ||
               access.presence != Presence::Present)
            {
                return std::nullopt;
            }

            return RegisterIndex(access.slot_idx);
        }

        Expected<ScopedRegister> codegen_node_into_a_register(int32_t node_idx)
        {
            uint32_t source_offset = av.source_offsets[node_idx];
            ValueLocation location =
                CL_TRY(codegen_node_into_value_location(node_idx));
            if(location.is_register())
            {
                return Expected<ScopedRegister>::ok(
                    {location.register_index, std::nullopt});
            }

            std::optional<TemporaryReg> temp{std::in_place, *code_obj};
            CL_TRY(code_obj->emit_star(source_offset, RegisterIndex(*temp)));
            return Expected<ScopedRegister>::ok(
                {RegisterIndex(*temp), std::move(temp)});
        }

        Expected<void> codegen_node_into_specific_register(int32_t node_idx,
                                                           RegisterIndex reg)
        {
            uint32_t source_offset = av.source_offsets[node_idx];
            ValueLocation location =
                CL_TRY(codegen_node_into_value_location(node_idx));
            if(location.is_register())
            {
                if(location.register_index != reg)
                {
                    CL_TRY(code_obj->emit_mov(source_offset, reg,
                                              location.register_index));
                }
                return Expected<void>::ok();
            }

            CL_TRY(code_obj->emit_star(source_offset, reg));
            return Expected<void>::ok();
        }

        // used for both regular binary expressions and augmented assignment, so
        // pull out
        Expected<void> codegen_binary_expression(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            if(is_membership_operator(kind.operator_kind))
            {
                ScopedRegister lhs_reg =
                    CL_TRY(codegen_node_into_a_register(children[0]));
                CL_TRY(codegen_node(children[1]));
                CL_TRY(code_obj->emit_operator_reg(
                    source_offset, Bytecode::Contains, lhs_reg.reg,
                    entry.bytecode_format));
                if(kind.operator_kind == AstOperatorKind::NOT_IN)
                {
                    CL_TRY(code_obj->emit_to_bool_not(source_offset));
                }
                else
                {
                    CL_TRY(code_obj->emit_to_bool(source_offset));
                }
                return Expected<void>::ok();
            }

            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            if(immediate.has_value())
            {
                CL_TRY(codegen_node(children[0]));
                CL_TRY(code_obj->emit_operator_smi(
                    source_offset, entry.binary_acc_smi, *immediate,
                    entry.bytecode_format));
            }
            else
            {
                ScopedRegister lhs_reg =
                    CL_TRY(codegen_node_into_a_register(children[0]));
                CL_TRY(codegen_node(children[1]));
                CL_TRY(code_obj->emit_operator_reg(source_offset,
                                                   entry.standard, lhs_reg.reg,
                                                   entry.bytecode_format));
            }
            return Expected<void>::ok();
        }

        Expected<void> codegen_comparison_fragment(int32_t node_idx,
                                                   int32_t recv, int32_t prod)
        {
            AstKind kind = av.kinds[node_idx];
            assert(kind.node_kind ==
                   AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT);
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            OpTableEntry entry = get_operator_entry(kind.operator_kind);

            CL_TRY(codegen_node(children[0]));
            if(prod >= 0)
            {
                CL_TRY(code_obj->emit_star(source_offset, prod));
            }
            CL_TRY(code_obj->emit_operator_reg(source_offset, entry.standard,
                                               recv, entry.bytecode_format));
            if(is_membership_operator(kind.operator_kind))
            {
                if(kind.operator_kind == AstOperatorKind::NOT_IN)
                {
                    CL_TRY(code_obj->emit_to_bool_not(source_offset));
                }
                else
                {
                    CL_TRY(code_obj->emit_to_bool(source_offset));
                }
            }
            return Expected<void>::ok();
        }

        Expected<void> codegen_function_definition(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            AstChildren param_children =
                CL_TRY(supported_runtime_parameter_order(av, children[0]));
            CodeObject *fun_obj = CL_TRY(
                codegen_function(av, code_obj->defining_module().extract(),
                                 code_obj, node_idx, language_mode));

            // stick this code object into the constant table, load it, and call
            // the
            uint32_t constant_idx =
                CL_TRY(code_obj->allocate_constant(Value::from_oop(fun_obj)));
            size_t first_default_idx =
                first_default_parameter_index(param_children);
            if(first_default_idx == param_children.size())
            {
                CL_TRY(code_obj->emit_create_function(source_offset,
                                                      constant_idx));
            }
            else
            {
                size_t last_default_idx =
                    last_default_parameter_index(param_children);
                size_t default_span_size =
                    last_default_idx - first_default_idx + 1;
                if(default_span_size > 64)
                {
                    return Expected<void>::raise_exception(
                        L"SyntaxError",
                        L"default parameter span exceeds mask capacity");
                }
                TemporaryReg default_values(*code_obj, default_span_size);
                uint64_t default_presence_mask = 0;
                fun_obj->function_signature.first_default_slot =
                    uint32_t(first_default_idx);
                for(size_t i = 0; i < default_span_size; ++i)
                {
                    int32_t param_idx = param_children[first_default_idx + i];
                    AstChildren default_children = av.children[param_idx];
                    if(default_children.empty())
                    {
                        CL_TRY(code_obj->emit_lda_none(source_offset));
                    }
                    else
                    {
                        assert(default_children.size() == 1);
                        CL_TRY(codegen_node_into_specific_register(
                            default_children[0], default_values + i));
                        default_presence_mask |= uint64_t(1) << i;
                        continue;
                    }
                    CL_TRY(
                        code_obj->emit_star(source_offset, default_values + i));
                }
                fun_obj->function_signature.default_presence_mask =
                    default_presence_mask;
                CL_TRY(code_obj->emit_create_tuple(
                    source_offset, default_values, default_span_size));

                TemporaryReg default_tuple(*code_obj);
                CL_TRY(code_obj->emit_star(source_offset, default_tuple));
                CL_TRY(code_obj->emit_create_function_with_defaults(
                    source_offset, constant_idx, default_tuple));
            }

            CL_TRY(emit_variable_store(source_offset, node_idx));
            return Expected<void>::ok();
        }

        size_t first_default_parameter_index(AstChildren param_children) const
        {
            for(size_t param_idx = 0; param_idx < param_children.size();
                ++param_idx)
            {
                int32_t node_idx = param_children[param_idx];
                if(av.kinds[node_idx].node_kind == AstNodeKind::PARAMETER &&
                   !av.children[node_idx].empty())
                {
                    return param_idx;
                }
            }
            return param_children.size();
        }

        size_t last_default_parameter_index(AstChildren param_children) const
        {
            for(size_t offset = 0; offset < param_children.size(); ++offset)
            {
                size_t param_idx = param_children.size() - offset - 1;
                int32_t node_idx = param_children[param_idx];
                if(av.kinds[node_idx].node_kind == AstNodeKind::PARAMETER &&
                   !av.children[node_idx].empty())
                {
                    return param_idx;
                }
            }
            return param_children.size();
        }

        Expected<void> codegen_class_definition(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t bases_idx = children[0];
            CodeObject *class_obj =
                CL_TRY(codegen_class(av, code_obj->defining_module().extract(),
                                     code_obj, node_idx, language_mode));

            uint32_t body_constant_idx =
                CL_TRY(code_obj->allocate_constant(Value::from_oop(class_obj)));
            AstChildren bases = av.children[bases_idx];
            uint32_t name_constant_idx =
                CL_TRY(code_obj->allocate_constant(av.constants[node_idx]));

            TemporaryReg base_regs(*code_obj,
                                   std::max<size_t>(bases.size(), 1));
            if(bases.empty())
            {
                uint32_t object_constant_idx =
                    CL_TRY(code_obj->allocate_constant(
                        Value::from_oop(active_vm()->object_class())));
                CL_TRY(code_obj->emit_lda_constant(source_offset,
                                                   object_constant_idx));
                CL_TRY(code_obj->emit_star(source_offset, base_regs));
            }
            else
            {
                for(size_t i = 0; i < bases.size(); ++i)
                {
                    if(av.kinds[bases[i]].node_kind !=
                       AstNodeKind::CALL_ARGUMENT_POSITIONAL)
                    {
                        return Expected<void>::raise_exception(
                            L"SyntaxError",
                            L"class keyword arguments are not implemented "
                            L"yet");
                    }
                    CL_TRY(codegen_node_into_specific_register(
                        call_argument_value(bases[i]), base_regs + i));
                }
            }
            CL_TRY(code_obj->emit_create_tuple(
                source_offset, base_regs, std::max<size_t>(bases.size(), 1)));
            TemporaryReg bases_tuple(*code_obj);
            CL_TRY(code_obj->emit_star(source_offset, bases_tuple));

            TemporaryReg call_args(*code_obj, ClassBodyParameterCount,
                                   RegisterAlignment::CallFrame);
            CL_TRY(
                code_obj->emit_lda_constant(source_offset, name_constant_idx));
            CL_TRY(code_obj->emit_star(source_offset, call_args));
            CL_TRY(
                code_obj->emit_mov(source_offset, call_args + 1, bases_tuple));
            CL_TRY(code_obj->emit_create_class(source_offset, body_constant_idx,
                                               call_args));

            CL_TRY(emit_variable_store(source_offset, node_idx));
            return Expected<void>::ok();
        }

        bool is_variable_reference_named(int32_t node_idx,
                                         const wchar_t *name) const
        {
            if(av.kinds[node_idx].node_kind !=
               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                return false;
            }
            return av.constants[node_idx].value() ==
                   interned_string(name).raw_value();
        }

        bool string_starts_with(TValue<String> str, const wchar_t *prefix) const
        {
            String *string = str.extract();
            size_t prefix_len = wcslen(prefix);
            return size_t(string->count.extract()) >= prefix_len &&
                   wcsncmp(string->data, prefix, prefix_len) == 0;
        }

        enum class TrustedCloverCall
        {
            None,
            CallSpecial,
            WriteStdout,
            Globals,
            Locals,
            Sqrt,
            TernaryPow,
            CanonicalizeHash,
        };

        Expected<TrustedCloverCall>
        trusted_clover_call_kind(int32_t node_idx) const
        {
            if(language_mode != LanguageMode::TrustedCloverExtensions)
            {
                return Expected<TrustedCloverCall>::ok(TrustedCloverCall::None);
            }

            const AstChildren &children = av.children[node_idx];
            if(av.kinds[children[0]].node_kind !=
               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                return Expected<TrustedCloverCall>::ok(TrustedCloverCall::None);
            }

            TValue<String> name =
                TValue<String>::from_value_assumed(av.constants[children[0]]);
            if(!string_starts_with(name, L"__clover_"))
            {
                return Expected<TrustedCloverCall>::ok(TrustedCloverCall::None);
            }

            Value name_value = name.raw_value();
            if(name_value ==
               interned_string(L"__clover_call_special__").raw_value())
            {
                return Expected<TrustedCloverCall>::ok(
                    TrustedCloverCall::CallSpecial);
            }
            if(name_value ==
               interned_string(L"__clover_write_stdout__").raw_value())
            {
                return Expected<TrustedCloverCall>::ok(
                    TrustedCloverCall::WriteStdout);
            }
            if(name_value == interned_string(L"__clover_globals__").raw_value())
            {
                return Expected<TrustedCloverCall>::ok(
                    TrustedCloverCall::Globals);
            }
            if(name_value == interned_string(L"__clover_locals__").raw_value())
            {
                return Expected<TrustedCloverCall>::ok(
                    TrustedCloverCall::Locals);
            }
            if(name_value == interned_string(L"__clover_sqrt__").raw_value())
            {
                return Expected<TrustedCloverCall>::ok(TrustedCloverCall::Sqrt);
            }
            if(name_value ==
               interned_string(L"__clover_ternary_pow__").raw_value())
            {
                return Expected<TrustedCloverCall>::ok(
                    TrustedCloverCall::TernaryPow);
            }
            if(name_value ==
               interned_string(L"__clover_canonicalize_hash__").raw_value())
            {
                return Expected<TrustedCloverCall>::ok(
                    TrustedCloverCall::CanonicalizeHash);
            }
            return Expected<TrustedCloverCall>::raise_exception(
                L"SyntaxError", L"unknown trusted __clover_* helper");
        }

        Expected<Value>
        literal_string_constant(int32_t node_idx,
                                const wchar_t *error_message) const
        {
            AstKind kind = av.kinds[node_idx];
            if(kind.node_kind != AstNodeKind::EXPRESSION_LITERAL ||
               kind.operator_kind != AstOperatorKind::STRING)
            {
                return Expected<Value>::raise_exception(L"SyntaxError",
                                                        error_message);
            }
            return Expected<Value>::ok(av.constants[node_idx]);
        }

        Expected<Value> builtin_class_constant_from_name_reference(
            int32_t node_idx, const wchar_t *error_message) const
        {
            if(av.kinds[node_idx].node_kind !=
               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                return Expected<Value>::raise_exception(L"SyntaxError",
                                                        error_message);
            }

            TValue<String> name = ast_string_constant(av, node_idx);
            Value value = active_vm()
                              ->global_builtins_module()
                              .extract()
                              ->get_own_property(name);
            if(!value.is_ptr() || value.get_ptr<Object>()->native_layout_id() !=
                                      NativeLayoutId::ClassObject)
            {
                return Expected<Value>::raise_exception(L"SyntaxError",
                                                        error_message);
            }
            return Expected<Value>::ok(value);
        }

        bool is_positional_call_argument(int32_t node_idx) const
        {
            return av.kinds[node_idx].node_kind ==
                   AstNodeKind::CALL_ARGUMENT_POSITIONAL;
        }

        bool is_keyword_call_argument(int32_t node_idx) const
        {
            return av.kinds[node_idx].node_kind ==
                   AstNodeKind::CALL_ARGUMENT_KEYWORD;
        }

        int32_t call_argument_value(int32_t node_idx) const
        {
            AstNodeKind node_kind = av.kinds[node_idx].node_kind;
            if(node_kind == AstNodeKind::CALL_ARGUMENT_POSITIONAL ||
               node_kind == AstNodeKind::CALL_ARGUMENT_KEYWORD)
            {
                return av.children[node_idx][0];
            }
            return node_idx;
        }

        uint32_t n_keyword_call_arguments(AstChildren args) const
        {
            uint32_t n_keywords = 0;
            for(int32_t arg: args)
            {
                if(is_keyword_call_argument(arg))
                {
                    ++n_keywords;
                }
            }
            return n_keywords;
        }

        Expected<void>
        require_positional_call_arguments(AstChildren args,
                                          const wchar_t *helper_name) const
        {
            for(int32_t arg: args)
            {
                if(!is_positional_call_argument(arg))
                {
                    std::wstring message = helper_name;
                    message += L" does not accept keyword arguments";
                    return Expected<void>::raise_exception(L"SyntaxError",
                                                           message.c_str());
                }
            }
            return Expected<void>::ok();
        }

        Expected<void>
        codegen_trusted_clover_call_special(uint32_t source_offset,
                                            AstChildren args)
        {
            CL_TRY(require_positional_call_arguments(
                args, L"__clover_call_special__"));
            if(args.size() < 4)
            {
                return Expected<void>::raise_exception(
                    L"SyntaxError",
                    L"__clover_call_special__ expects at least 4 arguments");
            }

            Value method_name = CL_TRY(literal_string_constant(
                call_argument_value(args[1]),
                L"__clover_call_special__ method name must be a string "
                L"literal"));
            Value missing_exception_type =
                CL_TRY(builtin_class_constant_from_name_reference(
                    call_argument_value(args[2]),
                    L"__clover_call_special__ exception type must be a builtin "
                    L"class name"));
            Value missing_exception_message = CL_TRY(literal_string_constant(
                call_argument_value(args[3]),
                L"__clover_call_special__ missing-method message must be a "
                L"string literal"));
            uint8_t method_name_idx =
                CL_TRY(code_obj->allocate_constant(method_name));
            uint8_t missing_exception_type_idx =
                CL_TRY(code_obj->allocate_constant(missing_exception_type));
            uint8_t missing_exception_message_idx =
                CL_TRY(code_obj->allocate_constant(missing_exception_message));

            TemporaryReg call_args(*code_obj, args.size() - 3,
                                   RegisterAlignment::CallFrame);
            CL_TRY(codegen_node_into_specific_register(
                call_argument_value(args[0]), call_args));
            for(size_t i = 4; i < args.size(); ++i)
            {
                CL_TRY(codegen_node_into_specific_register(
                    call_argument_value(args[i]), call_args + i - 3));
            }

            CL_TRY(code_obj->emit_call_special_method(
                source_offset, call_args, method_name_idx,
                uint8_t(args.size() - 4), missing_exception_type_idx,
                missing_exception_message_idx));
            return Expected<void>::ok();
        }

        Expected<void>
        codegen_trusted_clover_write_stdout(uint32_t source_offset,
                                            AstChildren args)
        {
            CL_TRY(require_positional_call_arguments(
                args, L"__clover_write_stdout__"));
            if(args.size() != 1)
            {
                return Expected<void>::raise_exception(
                    L"SyntaxError",
                    L"__clover_write_stdout__ expects exactly 1 argument");
            }

            CL_TRY(codegen_node(call_argument_value(args[0])));
            CL_TRY(code_obj->emit_write_stdout(source_offset));
            return Expected<void>::ok();
        }

        Expected<void> codegen_trusted_clover_intrinsic0(
            uint32_t source_offset, AstChildren args,
            RuntimeIntrinsic0 intrinsic, const wchar_t *helper_name)
        {
            CL_TRY(require_positional_call_arguments(args, helper_name));
            if(args.size() != 0)
            {
                std::wstring message = helper_name;
                message += L" expects exactly 0 arguments";
                return Expected<void>::raise_exception(L"SyntaxError",
                                                       message.c_str());
            }

            CL_TRY(code_obj->emit_call_runtime_intrinsic0(source_offset,
                                                          intrinsic));
            return Expected<void>::ok();
        }

        Expected<void> codegen_trusted_clover_sqrt(uint32_t source_offset,
                                                   AstChildren args)
        {
            CL_TRY(require_positional_call_arguments(args, L"__clover_sqrt__"));
            if(args.size() != 1)
            {
                return Expected<void>::raise_exception(
                    L"SyntaxError",
                    L"__clover_sqrt__ expects exactly 1 argument");
            }

            CL_TRY(codegen_node(call_argument_value(args[0])));
            CL_TRY(code_obj->emit_unary_op(source_offset, Bytecode::Sqrt,
                                           OperatorBytecodeFormat::Plain));
            return Expected<void>::ok();
        }

        Expected<void>
        codegen_trusted_clover_ternary_pow(uint32_t source_offset,
                                           AstChildren args)
        {
            CL_TRY(require_positional_call_arguments(
                args, L"__clover_ternary_pow__"));
            if(args.size() != 3)
            {
                return Expected<void>::raise_exception(
                    L"SyntaxError",
                    L"__clover_ternary_pow__ expects exactly 3 arguments");
            }

            TemporaryReg operands(*code_obj, 2);
            CL_TRY(codegen_node_into_specific_register(
                call_argument_value(args[0]), operands));
            CL_TRY(codegen_node_into_specific_register(
                call_argument_value(args[1]), operands + 1));
            CL_TRY(codegen_node(call_argument_value(args[2])));
            CL_TRY(code_obj->emit_ternary_operator(
                source_offset, Bytecode::TernaryPow, operands, operands + 1));
            return Expected<void>::ok();
        }

        Expected<void>
        codegen_trusted_clover_canonicalize_hash(uint32_t source_offset,
                                                 AstChildren args)
        {
            CL_TRY(require_positional_call_arguments(
                args, L"__clover_canonicalize_hash__"));
            if(args.size() != 1)
            {
                return Expected<void>::raise_exception(
                    L"SyntaxError",
                    L"__clover_canonicalize_hash__ expects exactly 1 "
                    L"argument");
            }

            CL_TRY(codegen_node(call_argument_value(args[0])));
            CL_TRY(code_obj->emit_unary_op(source_offset,
                                           Bytecode::CanonicalizeHash,
                                           OperatorBytecodeFormat::Plain));
            return Expected<void>::ok();
        }

        Expected<bool> try_codegen_trusted_clover_call(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            AstChildren args = av.children[children[1]];
            uint32_t source_offset = av.source_offsets[node_idx];

            switch(CL_TRY(trusted_clover_call_kind(node_idx)))
            {
                case TrustedCloverCall::None:
                    return Expected<bool>::ok(false);
                case TrustedCloverCall::CallSpecial:
                    CL_TRY(codegen_trusted_clover_call_special(source_offset,
                                                               args));
                    return Expected<bool>::ok(true);
                case TrustedCloverCall::WriteStdout:
                    CL_TRY(codegen_trusted_clover_write_stdout(source_offset,
                                                               args));
                    return Expected<bool>::ok(true);
                case TrustedCloverCall::Globals:
                    CL_TRY(codegen_trusted_clover_intrinsic0(
                        source_offset, args, RuntimeIntrinsic0::Globals,
                        L"__clover_globals__"));
                    return Expected<bool>::ok(true);
                case TrustedCloverCall::Locals:
                    CL_TRY(codegen_trusted_clover_intrinsic0(
                        source_offset, args, RuntimeIntrinsic0::Locals,
                        L"__clover_locals__"));
                    return Expected<bool>::ok(true);
                case TrustedCloverCall::Sqrt:
                    CL_TRY(codegen_trusted_clover_sqrt(source_offset, args));
                    return Expected<bool>::ok(true);
                case TrustedCloverCall::TernaryPow:
                    CL_TRY(codegen_trusted_clover_ternary_pow(source_offset,
                                                              args));
                    return Expected<bool>::ok(true);
                case TrustedCloverCall::CanonicalizeHash:
                    CL_TRY(codegen_trusted_clover_canonicalize_hash(
                        source_offset, args));
                    return Expected<bool>::ok(true);
            }
            __builtin_unreachable();
        }

        Expected<void> codegen_function_call(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            AstChildren args = av.children[children[1]];

            if(CL_TRY(try_codegen_trusted_clover_call(node_idx)))
            {
                return Expected<void>::ok();
            }

            if(av.kinds[children[0]].node_kind ==
               AstNodeKind::EXPRESSION_ATTRIBUTE)
            {
                AstChildren method_children = av.children[children[0]];
                uint8_t constant_idx = CL_TRY(
                    code_obj->allocate_constant(av.constants[children[0]]));

                uint32_t n_kw_args = n_keyword_call_arguments(args);
                if(n_kw_args > 0)
                {
                    uint32_t n_pos_args = args.size() - n_kw_args;
                    uint32_t kw_arg_idx = 0;

                    TemporaryReg keyword_value_regs(
                        *code_obj, std::max<uint32_t>(n_kw_args, 1));
                    TemporaryReg call_args(*code_obj, n_pos_args + 1,
                                           RegisterAlignment::CallFrame);
                    CL_TRY(codegen_node_into_specific_register(
                        method_children[0], call_args));
                    n_pos_args = 0;
                    for(int32_t arg: args)
                    {
                        if(is_positional_call_argument(arg))
                        {
                            CL_TRY(codegen_node_into_specific_register(
                                call_argument_value(arg),
                                call_args + 1 + n_pos_args));
                            ++n_pos_args;
                        }
                        else
                        {
                            assert(is_keyword_call_argument(arg));
                            CL_TRY(codegen_node_into_specific_register(
                                call_argument_value(arg),
                                keyword_value_regs + kw_arg_idx));
                            ++kw_arg_idx;
                        }
                    }
                    assert(kw_arg_idx == n_kw_args);

                    TValue<Tuple> keyword_names =
                        active_thread()->make_object_value<Tuple>(n_kw_args);
                    kw_arg_idx = 0;
                    for(int32_t arg: args)
                    {
                        if(is_keyword_call_argument(arg))
                        {
                            keyword_names.extract()->initialize_item_unchecked(
                                kw_arg_idx++, av.constants[arg].value());
                        }
                    }
                    assert(kw_arg_idx == n_kw_args);
                    uint8_t keyword_names_idx = CL_TRY(
                        code_obj->allocate_constant(keyword_names.raw_value()));
                    CL_TRY(code_obj->emit_call_method_attr_keyword(
                        source_offset, call_args, constant_idx,
                        uint8_t(n_pos_args), keyword_value_regs,
                        uint8_t(n_kw_args), keyword_names_idx));
                    return Expected<void>::ok();
                }

                TemporaryReg call_args(*code_obj, args.size() + 1,
                                       RegisterAlignment::CallFrame);
                CL_TRY(codegen_node_into_specific_register(method_children[0],
                                                           call_args));
                for(size_t i = 0; i < args.size(); ++i)
                {
                    CL_TRY(codegen_node_into_specific_register(
                        call_argument_value(args[i]), call_args + 1 + i));
                }
                CL_TRY(code_obj->emit_call_method_attr_positional(
                    source_offset, call_args, constant_idx, args.size()));
                return Expected<void>::ok();
            }

            // function itself
            TemporaryReg callable_reg(*code_obj);
            CL_TRY(
                codegen_node_into_specific_register(children[0], callable_reg));

            uint32_t n_kw_args = n_keyword_call_arguments(args);
            if(n_kw_args > 0)
            {
                uint32_t n_pos_args = args.size() - n_kw_args;
                uint32_t kw_arg_idx = 0;

                TemporaryReg keyword_value_regs(
                    *code_obj, std::max<uint32_t>(n_kw_args, 1));
                TemporaryReg call_args(*code_obj,
                                       std::max<uint32_t>(n_pos_args, 1),
                                       RegisterAlignment::CallFrame);
                n_pos_args = 0;
                for(int32_t arg: args)
                {
                    if(is_positional_call_argument(arg))
                    {
                        CL_TRY(codegen_node_into_specific_register(
                            call_argument_value(arg), call_args + n_pos_args));
                        ++n_pos_args;
                    }
                    else
                    {
                        assert(is_keyword_call_argument(arg));
                        CL_TRY(codegen_node_into_specific_register(
                            call_argument_value(arg),
                            keyword_value_regs + kw_arg_idx));
                        ++kw_arg_idx;
                    }
                }
                assert(kw_arg_idx == n_kw_args);

                TValue<Tuple> keyword_names =
                    active_thread()->make_object_value<Tuple>(n_kw_args);
                kw_arg_idx = 0;
                for(int32_t arg: args)
                {
                    if(is_keyword_call_argument(arg))
                    {
                        keyword_names.extract()->initialize_item_unchecked(
                            kw_arg_idx++, av.constants[arg].value());
                    }
                }
                assert(kw_arg_idx == n_kw_args);
                uint8_t keyword_names_idx = CL_TRY(
                    code_obj->allocate_constant(keyword_names.raw_value()));
                CL_TRY(code_obj->emit_call_keyword(
                    source_offset, callable_reg, call_args, uint8_t(n_pos_args),
                    keyword_value_regs, uint8_t(n_kw_args), keyword_names_idx));
                return Expected<void>::ok();
            }

            TemporaryReg call_args(*code_obj, std::max<size_t>(args.size(), 1),
                                   RegisterAlignment::CallFrame);
            for(size_t i = 0; i < args.size(); ++i)
            {
                CL_TRY(codegen_node_into_specific_register(
                    call_argument_value(args[i]), call_args + i));
            }
            CL_TRY(code_obj->emit_call_positional(source_offset, callable_reg,
                                                  call_args, args.size()));
            return Expected<void>::ok();
        }

        Expected<void> codegen_list_literal(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(*code_obj, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                CL_TRY(
                    codegen_node_into_specific_register(children[i], regs + i));
            }
            CL_TRY(code_obj->emit_create_list(source_offset, regs,
                                              children.size()));
            return Expected<void>::ok();
        }

        Expected<void> codegen_tuple_literal(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(*code_obj, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                CL_TRY(
                    codegen_node_into_specific_register(children[i], regs + i));
            }
            CL_TRY(code_obj->emit_create_tuple(source_offset, regs,
                                               children.size()));
            return Expected<void>::ok();
        }

        Expected<void> codegen_dict_literal(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(*code_obj, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                CL_TRY(
                    codegen_node_into_specific_register(children[i], regs + i));
            }
            CL_TRY(code_obj->emit_create_dict(source_offset, regs,
                                              children.size() / 2));
            return Expected<void>::ok();
        }

        Expected<void> codegen_slice_part_into_register(uint32_t source_offset,
                                                        int32_t part_idx,
                                                        uint32_t reg)
        {
            if(part_idx >= 0)
            {
                CL_TRY(codegen_node_into_specific_register(part_idx, reg));
            }
            else
            {
                CL_TRY(code_obj->emit_lda_none(source_offset));
                CL_TRY(code_obj->emit_star(source_offset, reg));
            }
            return Expected<void>::ok();
        }

        Expected<void> codegen_slice_literal(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            assert(children.size() == 3);
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg start_reg(*code_obj);
            CL_TRY(codegen_slice_part_into_register(source_offset, children[0],
                                                    start_reg));

            if(children[2] < 0)
            {
                if(children[1] >= 0)
                {
                    CL_TRY(codegen_node(children[1]));
                }
                else
                {
                    CL_TRY(code_obj->emit_lda_none(source_offset));
                }
                CL_TRY(code_obj->emit_create_binary_slice(source_offset,
                                                          start_reg));
                return Expected<void>::ok();
            }

            TemporaryReg stop_reg(*code_obj);
            CL_TRY(codegen_slice_part_into_register(source_offset, children[1],
                                                    stop_reg));
            CL_TRY(codegen_node(children[2]));
            CL_TRY(code_obj->emit_create_ternary_slice(source_offset, start_reg,
                                                       stop_reg));
            return Expected<void>::ok();
        }

        Expected<void> codegen_subscript_assignment(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t lhs_idx = children[0];
            AstChildren lhs_children = av.children[lhs_idx];

            if(kind.operator_kind == AstOperatorKind::NOP)
            {
                ScopedRegister value_reg =
                    CL_TRY(codegen_node_into_a_register(children[1]));
                ScopedRegister receiver_reg =
                    CL_TRY(codegen_node_into_a_register(lhs_children[0]));
                CL_TRY(codegen_node(lhs_children[1]));
                CL_TRY(code_obj->emit_set_item(source_offset, receiver_reg.reg,
                                               value_reg.reg));
                return Expected<void>::ok();
            }

            ScopedRegister receiver_reg =
                CL_TRY(codegen_node_into_a_register(lhs_children[0]));
            ScopedRegister key_reg =
                CL_TRY(codegen_node_into_a_register(lhs_children[1]));
            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            CL_TRY(code_obj->emit_ldar(source_offset, key_reg.reg));
            CL_TRY(code_obj->emit_get_item(source_offset, receiver_reg.reg));

            if(immediate.has_value())
            {
                CL_TRY(code_obj->emit_operator_smi(
                    source_offset, entry.binary_acc_smi, *immediate,
                    entry.bytecode_format));
            }
            else
            {
                TemporaryReg lhs_value_reg(*code_obj);
                CL_TRY(code_obj->emit_star(source_offset, lhs_value_reg));
                CL_TRY(codegen_node(children[1]));
                CL_TRY(code_obj->emit_operator_reg(
                    source_offset, entry.standard, lhs_value_reg,
                    entry.bytecode_format));
            }

            TemporaryReg value_reg(*code_obj);
            CL_TRY(code_obj->emit_star(source_offset, value_reg));
            CL_TRY(code_obj->emit_ldar(source_offset, key_reg.reg));
            CL_TRY(code_obj->emit_set_item(source_offset, receiver_reg.reg,
                                           value_reg));
            return Expected<void>::ok();
        }

        Expected<void> codegen_subscript_target_delete(uint32_t source_offset,
                                                       int32_t target_idx)
        {
            AstChildren target_children = av.children[target_idx];
            ScopedRegister receiver_reg =
                CL_TRY(codegen_node_into_a_register(target_children[0]));
            CL_TRY(codegen_node(target_children[1]));
            CL_TRY(code_obj->emit_del_item(source_offset, receiver_reg.reg));
            return Expected<void>::ok();
        }

        Expected<void> codegen_attribute_assignment(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t lhs_idx = children[0];
            AstChildren lhs_children = av.children[lhs_idx];
            uint8_t constant_idx =
                CL_TRY(code_obj->allocate_constant(av.constants[lhs_idx]));

            if(kind.operator_kind == AstOperatorKind::NOP)
            {
                CL_TRY(codegen_node(children[1]));
                std::optional<RegisterIndex> receiver_reg =
                    existing_register_location(lhs_children[0]);
                if(receiver_reg.has_value())
                {
                    CL_TRY(code_obj->emit_store_attr(
                        source_offset, *receiver_reg, constant_idx));
                    return Expected<void>::ok();
                }

                TemporaryReg value_reg(*code_obj);
                CL_TRY(code_obj->emit_star(source_offset, value_reg));
                ScopedRegister computed_receiver_reg =
                    CL_TRY(codegen_node_into_a_register(lhs_children[0]));
                CL_TRY(code_obj->emit_ldar(source_offset, value_reg));
                CL_TRY(code_obj->emit_store_attr(
                    source_offset, computed_receiver_reg.reg, constant_idx));
                return Expected<void>::ok();
            }

            ScopedRegister receiver_reg =
                CL_TRY(codegen_node_into_a_register(lhs_children[0]));
            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            CL_TRY(code_obj->emit_load_attr(source_offset, receiver_reg.reg,
                                            constant_idx));

            if(immediate.has_value())
            {
                CL_TRY(code_obj->emit_operator_smi(
                    source_offset, entry.binary_acc_smi, *immediate,
                    entry.bytecode_format));
            }
            else
            {
                TemporaryReg lhs_value_reg(*code_obj);
                CL_TRY(code_obj->emit_star(source_offset, lhs_value_reg));
                CL_TRY(codegen_node(children[1]));
                CL_TRY(code_obj->emit_operator_reg(
                    source_offset, entry.standard, lhs_value_reg,
                    entry.bytecode_format));
            }

            CL_TRY(code_obj->emit_store_attr(source_offset, receiver_reg.reg,
                                             constant_idx));
            return Expected<void>::ok();
        }

        Expected<void> codegen_attribute_target_delete(uint32_t source_offset,
                                                       int32_t target_idx)
        {
            AstChildren target_children = av.children[target_idx];
            uint8_t constant_idx =
                CL_TRY(code_obj->allocate_constant(av.constants[target_idx]));
            ScopedRegister receiver_reg =
                CL_TRY(codegen_node_into_a_register(target_children[0]));
            CL_TRY(code_obj->emit_del_attr(source_offset, receiver_reg.reg,
                                           constant_idx));
            return Expected<void>::ok();
        }

        std::optional<uint8_t> direct_range_call_arity(int32_t node_idx) const
        {
            if(av.kinds[node_idx].node_kind != AstNodeKind::EXPRESSION_CALL)
            {
                return std::nullopt;
            }

            AstChildren children = av.children[node_idx];
            int32_t callable_idx = children[0];
            if(av.kinds[callable_idx].node_kind !=
               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                return std::nullopt;
            }

            TValue<String> range_name = interned_string(L"range");
            if(av.constants[callable_idx].value() != range_name.raw_value())
            {
                return std::nullopt;
            }

            size_t n_args = av.children[children[1]].size();
            for(int32_t arg: av.children[children[1]])
            {
                if(!is_positional_call_argument(arg))
                {
                    return std::nullopt;
                }
            }
            if(n_args < 1 || n_args > 3)
            {
                return std::nullopt;
            }

            return uint8_t(n_args);
        }

        Expected<void> codegen_loop_body(int32_t body_idx,
                                         JumpTarget &break_target,
                                         JumpTarget &continue_target)
        {
            loop_targets.emplace_back(&break_target, &continue_target,
                                      active_cleanups.size());
            CL_TRY(codegen_node(body_idx));
            loop_targets.pop_back();
            return Expected<void>::ok();
        }

        bool node_has_raise_in_current_exception_context(int32_t node_idx) const
        {
            AstKind kind = av.kinds[node_idx];
            const AstChildren &children = av.children[node_idx];

            switch(kind.node_kind)
            {
                case AstNodeKind::STATEMENT_RAISE:
                    return true;

                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                case AstNodeKind::STATEMENT_CLASS_DEF:
                    return false;

                case AstNodeKind::STATEMENT_EXCEPT_HANDLER:
                    return false;

                case AstNodeKind::STATEMENT_TRY:
                    if(node_has_raise_in_current_exception_context(children[0]))
                    {
                        return true;
                    }
                    for(size_t i = 1; i < children.size(); ++i)
                    {
                        int32_t handler_idx = children[i];
                        if(av.kinds[handler_idx].node_kind ==
                           AstNodeKind::STATEMENT_FINALLY_HANDLER)
                        {
                            if(node_has_raise_in_current_exception_context(
                                   av.children[handler_idx][0]))
                            {
                                return true;
                            }
                            continue;
                        }
                        if(av.kinds[handler_idx].node_kind ==
                           AstNodeKind::STATEMENT_ELSE_HANDLER)
                        {
                            if(node_has_raise_in_current_exception_context(
                                   av.children[handler_idx][0]))
                            {
                                return true;
                            }
                            continue;
                        }
                        AstChildren handler_children = av.children[handler_idx];
                        if(handler_has_type(handler_children) &&
                           node_has_raise_in_current_exception_context(
                               handler_type_idx(handler_children)))
                        {
                            return true;
                        }
                    }
                    return false;

                default:
                    for(int32_t child_idx: children)
                    {
                        if(node_has_raise_in_current_exception_context(
                               child_idx))
                        {
                            return true;
                        }
                    }
                    return false;
            }
        }

        bool handler_needs_caught_exception(int32_t handler_idx) const
        {
            AstChildren children = av.children[handler_idx];
            return node_has_raise_in_current_exception_context(
                handler_body_idx(children));
        }

        bool try_has_finally(AstChildren try_children) const
        {
            return try_children.size() >= 2 &&
                   av.kinds[try_children.back()].node_kind ==
                       AstNodeKind::STATEMENT_FINALLY_HANDLER;
        }

        int32_t try_finally_body_idx(AstChildren try_children) const
        {
            assert(try_has_finally(try_children));
            AstChildren finally_children = av.children[try_children.back()];
            assert(finally_children.size() == 1);
            return finally_children[0];
        }

        bool try_has_else(AstChildren try_children,
                          size_t end_child_offset) const
        {
            return end_child_offset >= 2 &&
                   av.kinds[try_children[end_child_offset - 1]].node_kind ==
                       AstNodeKind::STATEMENT_ELSE_HANDLER;
        }

        int32_t try_else_body_idx(AstChildren try_children,
                                  size_t end_child_offset) const
        {
            assert(try_has_else(try_children, end_child_offset));
            AstChildren else_children =
                av.children[try_children[end_child_offset - 1]];
            assert(else_children.size() == 1);
            return else_children[0];
        }

        Expected<uint8_t> context_manager_protocol_type_error_idx()
        {
            return code_obj->allocate_constant(Value::from_oop(
                active_thread()->class_for_builtin_name(L"TypeError")));
        }

        Expected<uint8_t> context_manager_protocol_message_idx()
        {
            return code_obj->allocate_constant(
                interned_string(L"object does not support the context "
                                L"manager protocol"));
        }

        Expected<uint8_t> enter_method_name_idx()
        {
            return code_obj->allocate_constant(interned_string(L"__enter__"));
        }

        Expected<uint8_t> exit_method_name_idx()
        {
            return code_obj->allocate_constant(interned_string(L"__exit__"));
        }

        Expected<void> emit_context_exit_call(uint32_t source_offset,
                                              RegisterIndex manager_reg,
                                              uint32_t call_args)
        {
            CL_TRY(code_obj->emit_mov(source_offset, call_args, manager_reg));
            CL_TRY(code_obj->emit_call_special_method(
                source_offset, call_args, CL_TRY(exit_method_name_idx()), 3,
                CL_TRY(context_manager_protocol_type_error_idx()),
                CL_TRY(context_manager_protocol_message_idx())));
            return Expected<void>::ok();
        }

        Expected<void> emit_context_exit_none_call(uint32_t source_offset,
                                                   RegisterIndex manager_reg)
        {
            TemporaryReg call_args(*code_obj, 4, RegisterAlignment::CallFrame);
            for(uint8_t arg_idx = 0; arg_idx < 3; ++arg_idx)
            {
                CL_TRY(code_obj->emit_lda_none(source_offset));
                CL_TRY(code_obj->emit_star(source_offset,
                                           call_args + 1 + arg_idx));
            }
            CL_TRY(
                emit_context_exit_call(source_offset, manager_reg, call_args));
            return Expected<void>::ok();
        }

        Expected<void> emit_cleanup_body(CleanupContext ctx)
        {
            switch(ctx.kind)
            {
                case CleanupContext::Kind::FinallyBody:
                    CL_TRY(codegen_node(ctx.body_idx));
                    return Expected<void>::ok();
                case CleanupContext::Kind::WithExit:
                    CL_TRY(emit_context_exit_none_call(ctx.source_offset,
                                                       ctx.manager_reg));
                    return Expected<void>::ok();
            }
            __builtin_unreachable();
        }

        template <typename EmitAfterCleanups>
        Expected<void> emit_active_cleanups_until_and_then(
            size_t target_depth, EmitAfterCleanups emit_after_cleanups)
        {
            assert(target_depth <= active_cleanups.size());
            std::vector<CleanupContext> popped;
            std::vector<ExceptionTableRangeSuspension> suspensions;
            while(active_cleanups.size() > target_depth)
            {
                CleanupContext ctx = active_cleanups.back();
                active_cleanups.pop_back();
                popped.push_back(ctx);
                if(ctx.exception_range != nullptr)
                {
                    suspensions.push_back(ctx.exception_range->suspend());
                }
                CL_TRY(emit_cleanup_body(ctx));
            }

            CL_TRY(emit_after_cleanups());

            while(!popped.empty())
            {
                active_cleanups.push_back(popped.back());
                popped.pop_back();
            }
            return Expected<void>::ok();
        }

        Expected<void>
        emit_drain_active_exception_to_binding(uint32_t source_offset,
                                               int32_t name_idx)
        {
            const NameAccessAnalysis &access = store_access(name_idx);
            if(access.scope == BindingScope::Local)
            {
                CL_TRY(code_obj->emit_drain_active_exception_into(
                    source_offset, access.slot_idx));
                return Expected<void>::ok();
            }

            TemporaryReg saved_exception(*code_obj);
            CL_TRY(code_obj->emit_drain_active_exception_into(source_offset,
                                                              saved_exception));
            CL_TRY(code_obj->emit_ldar(source_offset, saved_exception));
            CL_TRY(emit_variable_store(source_offset, name_idx));
            return Expected<void>::ok();
        }

        Expected<void> emit_store_accumulator_to_target(uint32_t source_offset,
                                                        int32_t target_idx)
        {
            AstNodeKind target_kind = av.kinds[target_idx].node_kind;
            if(target_kind == AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                CL_TRY(emit_variable_store(source_offset, target_idx));
                return Expected<void>::ok();
            }

            if(target_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
            {
                AstChildren target_children = av.children[target_idx];
                uint8_t constant_idx = CL_TRY(
                    code_obj->allocate_constant(av.constants[target_idx]));
                std::optional<RegisterIndex> attr_receiver_reg =
                    existing_register_location(target_children[0]);
                if(attr_receiver_reg.has_value())
                {
                    CL_TRY(code_obj->emit_store_attr(
                        source_offset, *attr_receiver_reg, constant_idx));
                    return Expected<void>::ok();
                }

                TemporaryReg value_reg(*code_obj);
                CL_TRY(code_obj->emit_star(source_offset, value_reg));
                ScopedRegister receiver_reg =
                    CL_TRY(codegen_node_into_a_register(target_children[0]));
                CL_TRY(code_obj->emit_ldar(source_offset, value_reg));
                CL_TRY(code_obj->emit_store_attr(
                    source_offset, receiver_reg.reg, constant_idx));
                return Expected<void>::ok();
            }

            if(target_kind == AstNodeKind::EXPRESSION_BINARY &&
               av.kinds[target_idx].operator_kind == AstOperatorKind::SUBSCRIPT)
            {
                AstChildren target_children = av.children[target_idx];
                TemporaryReg value_reg(*code_obj);
                CL_TRY(code_obj->emit_star(source_offset, value_reg));
                ScopedRegister receiver_reg =
                    CL_TRY(codegen_node_into_a_register(target_children[0]));
                CL_TRY(codegen_node(target_children[1]));
                CL_TRY(code_obj->emit_set_item(source_offset, receiver_reg.reg,
                                               value_reg));
                return Expected<void>::ok();
            }

            return Expected<void>::raise_exception(
                L"SyntaxError",
                L"We don't support assignment to anything but simple "
                L"variables, attributes, and subscripts yet");
        }

        Expected<uint8_t> allocate_import_fromlist_constant(AstChildren aliases)
        {
            TValue<Tuple> fromlist =
                active_thread()->make_object_value<Tuple>(aliases.size());
            for(size_t idx = 0; idx < aliases.size(); ++idx)
            {
                fromlist.extract()->initialize_item_unchecked(
                    idx, av.constants[aliases[idx]]);
            }
            return code_obj->allocate_constant(fromlist.raw_value());
        }

        Expected<uint8_t> allocate_star_import_fromlist_constant()
        {
            TValue<Tuple> fromlist =
                active_thread()->make_object_value<Tuple>(1);
            fromlist.extract()->initialize_item_unchecked(
                0, interned_string(L"*").raw_value());
            return code_obj->allocate_constant(fromlist.raw_value());
        }

        std::vector<std::wstring> split_import_name(int32_t alias_idx)
        {
            std::wstring import_name = string_as_wchar_t(
                TValue<String>::from_value_assumed(av.constants[alias_idx]));
            std::vector<std::wstring> components;
            size_t start = 0;
            while(start <= import_name.size())
            {
                size_t dot = import_name.find(L'.', start);
                size_t end =
                    dot == std::wstring::npos ? import_name.size() : dot;
                components.push_back(import_name.substr(start, end - start));
                if(dot == std::wstring::npos)
                {
                    break;
                }
                start = dot + 1;
            }
            return components;
        }

        bool import_alias_has_explicit_target(int32_t alias_idx) const
        {
            return av.children[alias_idx].size() > 1;
        }

        Expected<void>
        codegen_with_statement_from_item(AstChildren with_children,
                                         size_t item_offset)
        {
            assert(item_offset + 1 < with_children.size());
            int32_t item_idx = with_children[item_offset];
            int32_t body_idx = with_children.back();
            AstChildren item_children = av.children[item_idx];
            assert(av.kinds[item_idx].node_kind == AstNodeKind::WITH_ITEM);
            assert(item_children.size() == 1 || item_children.size() == 2);

            uint32_t source_offset = av.source_offsets[item_idx];
            int32_t context_expr_idx = item_children[0];
            TemporaryReg manager_reg(*code_obj);
            CL_TRY(codegen_node_into_specific_register(context_expr_idx,
                                                       manager_reg));

            {
                TemporaryReg call_args(*code_obj, 1,
                                       RegisterAlignment::CallFrame);
                CL_TRY(
                    code_obj->emit_mov(source_offset, call_args, manager_reg));
                CL_TRY(code_obj->emit_call_special_method(
                    source_offset, call_args, CL_TRY(enter_method_name_idx()),
                    0, CL_TRY(context_manager_protocol_type_error_idx()),
                    CL_TRY(context_manager_protocol_message_idx())));
            }

            auto codegen_protected_body = [&]() -> Expected<void> {
                if(item_children.size() == 2)
                {
                    CL_TRY(emit_store_accumulator_to_target(source_offset,
                                                            item_children[1]));
                }
                if(item_offset + 2 == with_children.size())
                {
                    CL_TRY(codegen_node(body_idx));
                }
                else
                {
                    CL_TRY(codegen_with_statement_from_item(with_children,
                                                            item_offset + 1));
                }
                return Expected<void>::ok();
            };

            JumpTarget exceptional_target(code_obj);
            JumpTarget done_target(code_obj);
            {
                ExceptionTableRangeBuilder range(code_obj, exceptional_target);
                active_cleanups.push_back(CleanupContext::with_exit(
                    source_offset, RegisterIndex(manager_reg), &range));
                CL_TRY(codegen_protected_body());
                active_cleanups.pop_back();
                range.close();
            }

            CL_TRY(emit_context_exit_none_call(source_offset, manager_reg));
            CL_TRY(code_obj->emit_jump(source_offset, done_target));

            CL_TRY(exceptional_target.resolve());
            {
                TemporaryReg saved_exception(*code_obj);
                TemporaryReg call_args(*code_obj, 4,
                                       RegisterAlignment::CallFrame);
                uint8_t class_name_idx = CL_TRY(
                    code_obj->allocate_constant(interned_string(L"__class__")));

                CL_TRY(code_obj->emit_drain_active_exception_into(
                    source_offset, saved_exception));
                CL_TRY(code_obj->emit_load_attr(source_offset, saved_exception,
                                                class_name_idx));
                CL_TRY(code_obj->emit_star(source_offset, call_args + 1));
                CL_TRY(code_obj->emit_mov(source_offset, call_args + 2,
                                          saved_exception));
                CL_TRY(code_obj->emit_lda_none(source_offset));
                CL_TRY(code_obj->emit_star(source_offset, call_args + 3));
                CL_TRY(emit_context_exit_call(source_offset, manager_reg,
                                              call_args));
                CL_TRY(code_obj->emit_jump_if_true(source_offset, done_target));
                CL_TRY(code_obj->emit_ldar(source_offset, saved_exception));
                CL_TRY(code_obj->emit_raise_unwind(source_offset));
            }

            CL_TRY(done_target.resolve());
            return Expected<void>::ok();
        }

        Expected<void> codegen_with_statement(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            assert(children.size() >= 2);
            CL_TRY(codegen_with_statement_from_item(children, 0));
            return Expected<void>::ok();
        }

        Expected<void> codegen_try_finally_statement(int32_t node_idx)
        {
            const AstChildren &children = av.children[node_idx];
            assert(children.size() >= 2);
            assert(try_has_finally(children));
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t body_idx = children[0];
            int32_t finally_body_idx = try_finally_body_idx(children);

            auto codegen_protected_body = [&]() -> Expected<void> {
                if(children.size() == 2)
                {
                    CL_TRY(codegen_node(body_idx));
                }
                else
                {
                    CL_TRY(codegen_try_except_statement(source_offset, children,
                                                        children.size() - 1));
                }
                return Expected<void>::ok();
            };

            JumpTarget exceptional_target(code_obj);
            JumpTarget done_target(code_obj);
            {
                ExceptionTableRangeBuilder range(code_obj, exceptional_target);
                active_cleanups.push_back(
                    CleanupContext::finally_body(finally_body_idx, &range));
                CL_TRY(codegen_protected_body());
                active_cleanups.pop_back();
                range.close();
            }

            CL_TRY(codegen_node(finally_body_idx));
            CL_TRY(code_obj->emit_jump(source_offset, done_target));

            CL_TRY(exceptional_target.resolve());
            {
                TemporaryReg saved_exception(*code_obj);
                CL_TRY(code_obj->emit_drain_active_exception_into(
                    source_offset, saved_exception));
                caught_exception_regs.push_back(RegisterIndex(saved_exception));
                CL_TRY(codegen_node(finally_body_idx));
                caught_exception_regs.pop_back();
                CL_TRY(code_obj->emit_ldar(source_offset, saved_exception));
                CL_TRY(code_obj->emit_raise_unwind(source_offset));
            }

            CL_TRY(done_target.resolve());
            return Expected<void>::ok();
        }

        Expected<void> codegen_try_except_statement(uint32_t source_offset,
                                                    AstChildren children,
                                                    size_t end_child_offset)
        {
            assert(end_child_offset >= 2);
            assert(end_child_offset <= children.size());
            int32_t body_idx = children[0];
            int32_t else_body_idx = -1;
            size_t handler_end_child_offset = end_child_offset;
            if(try_has_else(children, end_child_offset))
            {
                else_body_idx = try_else_body_idx(children, end_child_offset);
                --handler_end_child_offset;
            }
            assert(handler_end_child_offset >= 2);

            JumpTarget handler_target(code_obj);
            JumpTarget done_target(code_obj);
            {
                ExceptionTableRangeBuilder range(code_obj, handler_target);
                CL_TRY(codegen_node(body_idx));
                range.close();
            }
            if(else_body_idx >= 0)
            {
                CL_TRY(codegen_node(else_body_idx));
            }
            CL_TRY(code_obj->emit_jump(source_offset, done_target));

            CL_TRY(handler_target.resolve());
            bool has_bare_handler = false;
            for(size_t child_offset = 1;
                child_offset < handler_end_child_offset; ++child_offset)
            {
                int32_t handler_idx = children[child_offset];
                AstChildren handler_children = av.children[handler_idx];
                assert(av.kinds[handler_idx].node_kind ==
                       AstNodeKind::STATEMENT_EXCEPT_HANDLER);
                assert(handler_children.size() >= 1 &&
                       handler_children.size() <= 3);

                int32_t body_idx = handler_body_idx(handler_children);
                uint32_t handler_source_offset = av.source_offsets[handler_idx];
                bool needs_original_exception =
                    handler_needs_caught_exception(handler_idx);
                bool binds_exception_name = handler_has_name(handler_children);
                if(handler_has_type(handler_children))
                {
                    JumpTarget no_match_target(code_obj);
                    CL_TRY(codegen_node(handler_type_idx(handler_children)));
                    CL_TRY(code_obj->emit_active_exception_is_instance(
                        handler_source_offset));
                    CL_TRY(code_obj->emit_jump_if_false(handler_source_offset,
                                                        no_match_target));
                    if(needs_original_exception)
                    {
                        TemporaryReg saved_exception(*code_obj);
                        CL_TRY(code_obj->emit_drain_active_exception_into(
                            handler_source_offset, saved_exception));
                        if(binds_exception_name)
                        {
                            CL_TRY(code_obj->emit_ldar(handler_source_offset,
                                                       saved_exception));
                            CL_TRY(emit_variable_store(
                                handler_source_offset,
                                handler_name_idx(handler_children)));
                        }
                        caught_exception_regs.push_back(
                            RegisterIndex(saved_exception));
                        CL_TRY(codegen_node(body_idx));
                        caught_exception_regs.pop_back();
                        CL_TRY(code_obj->emit_jump(handler_source_offset,
                                                   done_target));
                    }
                    else if(binds_exception_name)
                    {
                        CL_TRY(emit_drain_active_exception_to_binding(
                            handler_source_offset,
                            handler_name_idx(handler_children)));
                        CL_TRY(codegen_node(body_idx));
                        CL_TRY(code_obj->emit_jump(handler_source_offset,
                                                   done_target));
                    }
                    else
                    {
                        CL_TRY(code_obj->emit_clear_active_exception(
                            handler_source_offset));
                        CL_TRY(codegen_node(body_idx));
                        CL_TRY(code_obj->emit_jump(handler_source_offset,
                                                   done_target));
                    }

                    CL_TRY(no_match_target.resolve());
                    continue;
                }

                assert(child_offset == handler_end_child_offset - 1);
                has_bare_handler = true;
                if(needs_original_exception)
                {
                    TemporaryReg saved_exception(*code_obj);
                    CL_TRY(code_obj->emit_drain_active_exception_into(
                        handler_source_offset, saved_exception));
                    if(binds_exception_name)
                    {
                        CL_TRY(code_obj->emit_ldar(handler_source_offset,
                                                   saved_exception));
                        CL_TRY(emit_variable_store(
                            handler_source_offset,
                            handler_name_idx(handler_children)));
                    }
                    caught_exception_regs.push_back(
                        RegisterIndex(saved_exception));
                    CL_TRY(codegen_node(body_idx));
                    caught_exception_regs.pop_back();
                    CL_TRY(code_obj->emit_jump(handler_source_offset,
                                               done_target));
                }
                else if(binds_exception_name)
                {
                    CL_TRY(emit_drain_active_exception_to_binding(
                        handler_source_offset,
                        handler_name_idx(handler_children)));
                    CL_TRY(codegen_node(body_idx));
                    CL_TRY(code_obj->emit_jump(handler_source_offset,
                                               done_target));
                }
                else
                {
                    CL_TRY(code_obj->emit_clear_active_exception(
                        handler_source_offset));
                    CL_TRY(codegen_node(body_idx));
                    CL_TRY(code_obj->emit_jump(handler_source_offset,
                                               done_target));
                }
                break;
            }

            if(!has_bare_handler)
            {
                CL_TRY(code_obj->emit_reraise_active_exception(source_offset));
            }

            CL_TRY(done_target.resolve());
            return Expected<void>::ok();
        }

        Expected<void> codegen_try_statement(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            assert(children.size() >= 2);
            if(try_has_finally(children))
            {
                CL_TRY(codegen_try_finally_statement(node_idx));
                return Expected<void>::ok();
            }

            CL_TRY(codegen_try_except_statement(av.source_offsets[node_idx],
                                                children, children.size()));
            return Expected<void>::ok();
        }

        Expected<void> codegen_iterator_driven_for_loop(
            uint32_t source_offset, int32_t target_idx, int32_t body_idx,
            uint32_t iterator_reg, JumpTarget &else_target,
            JumpTarget &break_target)
        {
            JumpTarget loop_start_target(code_obj);
            JumpTarget continue_target(code_obj);
            JumpTarget stop_iteration_handler_target(code_obj);
            JumpTarget propagate_exception_target(code_obj);
            uint8_t next_constant_idx = CL_TRY(
                code_obj->allocate_constant(interned_string(L"__next__")));
            uint8_t not_iterator_type_constant_idx =
                CL_TRY(code_obj->allocate_constant(Value::from_oop(
                    active_thread()->class_for_builtin_name(L"TypeError"))));
            uint8_t not_iterator_message_constant_idx =
                CL_TRY(code_obj->allocate_constant(
                    interned_string(L"object is not an iterator")));
            uint8_t stop_iteration_constant_idx =
                CL_TRY(code_obj->allocate_constant(
                    Value::from_oop(active_thread()->class_for_builtin_name(
                        L"StopIteration"))));

            CL_TRY(loop_start_target.resolve());
            {
                TemporaryReg call_args(*code_obj, 1,
                                       RegisterAlignment::CallFrame);
                CL_TRY(
                    code_obj->emit_mov(source_offset, call_args, iterator_reg));
                ExceptionTableRangeBuilder range(code_obj,
                                                 stop_iteration_handler_target);
                CL_TRY(code_obj->emit_call_special_method(
                    source_offset, call_args, next_constant_idx, 0,
                    not_iterator_type_constant_idx,
                    not_iterator_message_constant_idx));
                range.close();
            }
            CL_TRY(emit_variable_store(source_offset, target_idx));

            CL_TRY(codegen_loop_body(body_idx, break_target, continue_target));

            CL_TRY(continue_target.resolve());
            CL_TRY(code_obj->emit_jump(source_offset, loop_start_target));

            CL_TRY(stop_iteration_handler_target.resolve());
            CL_TRY(code_obj->emit_lda_constant(source_offset,
                                               stop_iteration_constant_idx));
            CL_TRY(code_obj->emit_active_exception_is_instance(source_offset));
            CL_TRY(code_obj->emit_jump_if_false(source_offset,
                                                propagate_exception_target));
            CL_TRY(code_obj->emit_clear_active_exception(source_offset));
            CL_TRY(code_obj->emit_jump(source_offset, else_target));

            CL_TRY(propagate_exception_target.resolve());
            CL_TRY(code_obj->emit_reraise_active_exception(source_offset));
            return Expected<void>::ok();
        }

        Expected<void> codegen_direct_range_for_loop(int32_t node_idx,
                                                     int32_t target_idx,
                                                     uint8_t n_args)
        {
            const AstChildren &children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t iterable_idx = children[1];
            int32_t body_idx = children[2];
            int32_t else_idx = children.size() == 4 ? children[3] : -1;
            AstChildren call_children = av.children[iterable_idx];
            AstChildren args = av.children[call_children[1]];

            TemporaryReg range_regs(*code_obj, 1 + n_args);
            TemporaryReg iterator_reg(*code_obj);
            JumpTarget generic_fallback_target(code_obj);
            JumpTarget fast_loop_start_target(code_obj);
            JumpTarget fast_continue_target(code_obj);
            JumpTarget else_target(code_obj);
            JumpTarget break_target(code_obj);

            CL_TRY(codegen_node_into_specific_register(call_children[0],
                                                       range_regs + 0));
            for(size_t i = 0; i < args.size(); ++i)
            {
                CL_TRY(codegen_node_into_specific_register(
                    call_argument_value(args[i]), range_regs + 1 + i));
            }

            Bytecode prep_opcode = Bytecode::Invalid;
            Bytecode iter_opcode = Bytecode::Invalid;
            switch(n_args)
            {
                case 1:
                    prep_opcode = Bytecode::ForPrepRange1;
                    iter_opcode = Bytecode::ForIterRange1;
                    break;
                case 2:
                    prep_opcode = Bytecode::ForPrepRange2;
                    iter_opcode = Bytecode::ForIterRange1;
                    break;
                case 3:
                    prep_opcode = Bytecode::ForPrepRange3;
                    iter_opcode = Bytecode::ForIterRangeStep;
                    break;
                default:
                    assert(false);
            }

            CL_TRY(code_obj->emit_for_prep_range(source_offset, prep_opcode,
                                                 range_regs,
                                                 generic_fallback_target));

            CL_TRY(fast_loop_start_target.resolve());
            CL_TRY(code_obj->emit_for_iter_range(source_offset, iter_opcode,
                                                 range_regs, else_target));
            CL_TRY(emit_variable_store(source_offset, target_idx));
            CL_TRY(codegen_loop_body(body_idx, break_target,
                                     fast_continue_target));
            CL_TRY(fast_continue_target.resolve());
            CL_TRY(code_obj->emit_jump(source_offset, fast_loop_start_target));

            CL_TRY(generic_fallback_target.resolve());
            {
                TemporaryReg call_args(*code_obj, n_args,
                                       RegisterAlignment::CallFrame);
                for(size_t i = 0; i < args.size(); ++i)
                {
                    CL_TRY(code_obj->emit_mov(source_offset, call_args + i,
                                              range_regs + 1 + i));
                }
                CL_TRY(code_obj->emit_call_positional(source_offset, range_regs,
                                                      call_args, n_args));
            }
            uint8_t iter_constant_idx = CL_TRY(
                code_obj->allocate_constant(interned_string(L"__iter__")));
            uint8_t not_iterable_type_constant_idx =
                CL_TRY(code_obj->allocate_constant(Value::from_oop(
                    active_thread()->class_for_builtin_name(L"TypeError"))));
            uint8_t not_iterable_message_constant_idx =
                CL_TRY(code_obj->allocate_constant(
                    interned_string(L"object is not iterable")));
            {
                TemporaryReg call_args(*code_obj, 1,
                                       RegisterAlignment::CallFrame);
                CL_TRY(code_obj->emit_star(source_offset, call_args));
                CL_TRY(code_obj->emit_call_special_method(
                    source_offset, call_args, iter_constant_idx, 0,
                    not_iterable_type_constant_idx,
                    not_iterable_message_constant_idx));
            }
            CL_TRY(code_obj->emit_star(source_offset, iterator_reg));
            CL_TRY(codegen_iterator_driven_for_loop(source_offset, target_idx,
                                                    body_idx, iterator_reg,
                                                    else_target, break_target));

            CL_TRY(else_target.resolve());
            if(else_idx >= 0)
            {
                CL_TRY(codegen_node(else_idx));
            }
            CL_TRY(break_target.resolve());
            return Expected<void>::ok();
        }

        Expected<void> codegen_node(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            switch(kind.node_kind)
            {

                case AstNodeKind::EXPRESSION_VARIABLE_REFERENCE:
                    CL_TRY(emit_variable_load(source_offset, node_idx));
                    break;

                case AstNodeKind::CALL_ARGUMENT_POSITIONAL:
                case AstNodeKind::CALL_ARGUMENT_KEYWORD:
                    return Expected<void>::raise_exception(
                        L"SystemError",
                        L"call argument nodes must be lowered by call codegen");

                case AstNodeKind::STATEMENT_ASSIGN:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    {

                        int32_t lhs_idx = children[0];
                        AstNodeKind lhs_kind = av.kinds[lhs_idx].node_kind;
                        if(lhs_kind !=
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE &&
                           lhs_kind != AstNodeKind::EXPRESSION_ATTRIBUTE &&
                           !(lhs_kind == AstNodeKind::EXPRESSION_BINARY &&
                             av.kinds[lhs_idx].operator_kind ==
                                 AstOperatorKind::SUBSCRIPT))
                        {
                            return Expected<void>::raise_exception(
                                L"SyntaxError",
                                L"We don't support assignment to anything but "
                                L"simple variables, attributes, and "
                                L"subscripts yet");
                        }

                        if(lhs_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                        {
                            CL_TRY(codegen_attribute_assignment(node_idx));
                            break;
                        }
                        if(lhs_kind == AstNodeKind::EXPRESSION_BINARY)
                        {
                            CL_TRY(codegen_subscript_assignment(node_idx));
                            break;
                        }

                        // augmented assignment
                        if(kind.operator_kind != AstOperatorKind::NOP)
                        {
                            CL_TRY(codegen_binary_expression(node_idx));
                        }
                        else
                        {
                            const NameAccessAnalysis &access =
                                store_access(lhs_idx);
                            if(kind.node_kind ==
                                   AstNodeKind::STATEMENT_ASSIGN &&
                               access.scope == BindingScope::Local)
                            {
                                CL_TRY(codegen_node_into_specific_register(
                                    children[1], access.slot_idx));
                                break;
                            }

                            // just compute the RHS
                            CL_TRY(codegen_node(children[1]));
                        }
                        CL_TRY(emit_variable_store(source_offset, lhs_idx));
                        break;
                    }

                case AstNodeKind::STATEMENT_IMPORT:
                    for(int32_t alias_idx: children)
                    {
                        int32_t target_idx = av.children[alias_idx][0];
                        uint8_t name_idx = CL_TRY(code_obj->allocate_constant(
                            av.constants[alias_idx]));
                        CL_TRY(code_obj->emit_lda_none(source_offset));
                        CL_TRY(code_obj->emit_import_name(source_offset,
                                                          name_idx, 0));
                        if(import_alias_has_explicit_target(alias_idx))
                        {
                            std::vector<std::wstring> components =
                                split_import_name(alias_idx);
                            for(size_t component_idx = 1;
                                component_idx < components.size();
                                ++component_idx)
                            {
                                uint8_t component_name_idx = CL_TRY(
                                    code_obj->allocate_constant(interned_string(
                                        components[component_idx])));
                                CL_TRY(code_obj->emit_import_from(
                                    source_offset, component_name_idx));
                            }
                        }
                        CL_TRY(emit_store_accumulator_to_target(source_offset,
                                                                target_idx));
                    }
                    break;

                case AstNodeKind::STATEMENT_IMPORT_FROM:
                    {
                        bool is_star_import = children.size() == 2 &&
                                              av.kinds[children[1]].node_kind ==
                                                  AstNodeKind::IMPORT_STAR;
                        if(is_star_import && mode() != CodegenMode::Module)
                        {
                            return Expected<void>::raise_exception(
                                L"SyntaxError",
                                L"import * only allowed at module level");
                        }
                        AstChildren aliases;
                        if(!is_star_import)
                        {
                            for(size_t child_offset = 1;
                                child_offset < children.size(); ++child_offset)
                            {
                                aliases.push_back(children[child_offset]);
                            }
                        }
                        uint8_t fromlist_idx = CL_TRY(
                            is_star_import
                                ? allocate_star_import_fromlist_constant()
                                : allocate_import_fromlist_constant(aliases));
                        uint8_t module_name_idx =
                            CL_TRY(code_obj->allocate_constant(
                                av.constants[node_idx]));
                        int64_t level =
                            av.constants[children[0]].value().get_smi();
                        if(level < 0 || level > 255)
                        {
                            return Expected<void>::raise_exception(
                                L"SyntaxError",
                                L"relative import level out of range");
                        }
                        CL_TRY(code_obj->emit_lda_constant(source_offset,
                                                           fromlist_idx));
                        CL_TRY(code_obj->emit_import_name(
                            source_offset, module_name_idx,
                            static_cast<uint8_t>(level)));
                        if(is_star_import)
                        {
                            CL_TRY(code_obj->emit_call_runtime_intrinsic0(
                                source_offset, RuntimeIntrinsic0::ImportStar));
                            break;
                        }
                        if(aliases.size() == 1)
                        {
                            int32_t alias_idx = aliases[0];
                            uint8_t name_idx =
                                CL_TRY(code_obj->allocate_constant(
                                    av.constants[alias_idx]));
                            CL_TRY(code_obj->emit_import_from(source_offset,
                                                              name_idx));
                            CL_TRY(emit_store_accumulator_to_target(
                                source_offset, av.children[alias_idx][0]));
                        }
                        else
                        {
                            TemporaryReg module_reg(*code_obj);
                            CL_TRY(
                                code_obj->emit_star(source_offset, module_reg));
                            for(int32_t alias_idx: aliases)
                            {
                                uint8_t name_idx =
                                    CL_TRY(code_obj->allocate_constant(
                                        av.constants[alias_idx]));
                                CL_TRY(code_obj->emit_ldar(source_offset,
                                                           module_reg));
                                CL_TRY(code_obj->emit_import_from(source_offset,
                                                                  name_idx));
                                CL_TRY(emit_store_accumulator_to_target(
                                    source_offset, av.children[alias_idx][0]));
                            }
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_ANN_ASSIGN:
                    if(children.size() == 3)
                    {
                        int32_t lhs_idx = children[0];
                        AstNodeKind lhs_kind = av.kinds[lhs_idx].node_kind;
                        if(lhs_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                        {
                            CL_TRY(codegen_node(children[2]));
                            CL_TRY(emit_store_accumulator_to_target(
                                source_offset, lhs_idx));
                            break;
                        }
                        if(lhs_kind == AstNodeKind::EXPRESSION_BINARY)
                        {
                            CL_TRY(codegen_node(children[2]));
                            CL_TRY(emit_store_accumulator_to_target(
                                source_offset, lhs_idx));
                            break;
                        }
                        CL_TRY(codegen_node(children[2]));
                        CL_TRY(emit_variable_store(source_offset, lhs_idx));
                    }
                    else if(!ann_assign_is_simple(node_idx))
                    {
                        int32_t lhs_idx = children[0];
                        AstNodeKind lhs_kind = av.kinds[lhs_idx].node_kind;
                        if(lhs_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                        {
                            CL_TRY(codegen_node(av.children[lhs_idx][0]));
                            break;
                        }
                        if(lhs_kind == AstNodeKind::EXPRESSION_BINARY &&
                           av.kinds[lhs_idx].operator_kind ==
                               AstOperatorKind::SUBSCRIPT)
                        {
                            AstChildren lhs_children = av.children[lhs_idx];
                            CL_TRY(codegen_node(lhs_children[0]));
                            CL_TRY(codegen_node(lhs_children[1]));
                            break;
                        }
                        CL_TRY(codegen_node(lhs_idx));
                    }
                    break;

                case AstNodeKind::STATEMENT_DEL:
                    for(int32_t target_idx: children)
                    {
                        AstNodeKind target_kind =
                            av.kinds[target_idx].node_kind;
                        if(target_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                        {
                            CL_TRY(codegen_attribute_target_delete(
                                source_offset, target_idx));
                            continue;
                        }
                        if(target_kind == AstNodeKind::EXPRESSION_BINARY &&
                           av.kinds[target_idx].operator_kind ==
                               AstOperatorKind::SUBSCRIPT)
                        {
                            CL_TRY(codegen_subscript_target_delete(
                                source_offset, target_idx));
                            continue;
                        }
                        if(target_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            CL_TRY(emit_variable_delete(source_offset,
                                                        target_idx));
                            continue;
                        }
                        return Expected<void>::raise_exception(
                            L"SyntaxError",
                            L"We don't support del targets except variables "
                            L"and attributes and subscripts yet");
                    }
                    break;

                case AstNodeKind::STATEMENT_ASSERT:
                    {
                        JumpTarget ok_target(code_obj);
                        CL_TRY(codegen_node(children[0]));
                        CL_TRY(code_obj->emit_jump_if_true(source_offset,
                                                           ok_target));
                        if(children.size() == 1)
                        {
                            CL_TRY(code_obj->emit_raise_assertion_error(
                                source_offset));
                        }
                        else
                        {
                            CL_TRY(codegen_node(children[1]));
                            CL_TRY(
                                code_obj
                                    ->emit_raise_assertion_error_with_message(
                                        source_offset));
                        }
                        CL_TRY(ok_target.resolve());
                    }
                    break;

                case AstNodeKind::STATEMENT_RAISE:
                    if(children.empty())
                    {
                        if(caught_exception_regs.empty())
                        {
                            CL_TRY(code_obj->emit_raise_bare(source_offset));
                            break;
                        }
                        CL_TRY(code_obj->emit_ldar(
                            source_offset, caught_exception_regs.back()));
                        CL_TRY(code_obj->emit_raise_unwind(source_offset));
                    }
                    else
                    {
                        CL_TRY(codegen_node(children[0]));
                        if(caught_exception_regs.empty())
                        {
                            CL_TRY(code_obj->emit_raise_unwind(source_offset));
                        }
                        else
                        {
                            CL_TRY(code_obj->emit_raise_unwind_with_context(
                                source_offset, caught_exception_regs.back()));
                        }
                    }
                    break;

                case AstNodeKind::EXPRESSION_BINARY:
                    if(kind.operator_kind == AstOperatorKind::SUBSCRIPT)
                    {
                        ScopedRegister receiver_reg =
                            CL_TRY(codegen_node_into_a_register(children[0]));
                        CL_TRY(codegen_node(children[1]));
                        CL_TRY(code_obj->emit_get_item(source_offset,
                                                       receiver_reg.reg));
                        break;
                    }
                    CL_TRY(codegen_binary_expression(node_idx));
                    break;

                case AstNodeKind::EXPRESSION_UNARY:
                    {
                        OpTableEntry entry =
                            get_operator_entry(kind.operator_kind);
                        CL_TRY(codegen_node(children[0]));
                        CL_TRY(code_obj->emit_unary_op(source_offset,
                                                       entry.standard,
                                                       entry.bytecode_format));
                        break;
                    }
                case AstNodeKind::EXPRESSION_LITERAL:
                    {

                        switch(kind.operator_kind)
                        {
                            case AstOperatorKind::NONE:
                                CL_TRY(code_obj->emit_lda_none(source_offset));
                                break;
                            case AstOperatorKind::TRUE:
                                CL_TRY(code_obj->emit_lda_true(source_offset));
                                break;
                            case AstOperatorKind::FALSE:
                                CL_TRY(code_obj->emit_lda_false(source_offset));
                                break;
                            case AstOperatorKind::ELLIPSIS:
                                {
                                    uint32_t constant_idx =
                                        CL_TRY(code_obj->allocate_constant(
                                            Value::Ellipsis()));
                                    CL_TRY(code_obj->emit_lda_constant(
                                        source_offset, constant_idx));
                                    break;
                                }

                            case AstOperatorKind::NUMBER:
                                {
                                    Value val = av.constants[node_idx];
                                    if(val.is_smi8())
                                    {
                                        CL_TRY(code_obj->emit_lda_smi(
                                            source_offset, val.get_smi()));
                                    }
                                    else
                                    {
                                        uint32_t constant_idx = CL_TRY(
                                            code_obj->allocate_constant(val));
                                        CL_TRY(code_obj->emit_lda_constant(
                                            source_offset, constant_idx));
                                        break;
                                    }
                                    break;
                                }

                            case AstOperatorKind::STRING:
                            case AstOperatorKind::BYTES:
                                {
                                    uint32_t constant_idx =
                                        CL_TRY(code_obj->allocate_constant(
                                            av.constants[node_idx]));
                                    CL_TRY(code_obj->emit_lda_constant(
                                        source_offset, constant_idx));
                                    break;
                                }
                            default:
                                break;
                        }
                        break;
                    }

                case AstNodeKind::EXPRESSION_COMPARISON:
                    {
                        JumpTarget skip_target(code_obj);

                        ScopedRegister recv_reg =
                            CL_TRY(codegen_node_into_a_register(children[0]));
                        TemporaryReg prod_reg(*code_obj);
                        int32_t recv = recv_reg.reg;
                        int32_t prod = prod_reg;
                        for(size_t i = 1; i < children.size(); ++i)
                        {
                            bool last = i == children.size() - 1;
                            if(last)
                                prod = -1;

                            CL_TRY(codegen_comparison_fragment(children[i],
                                                               recv, prod));

                            if(!last)
                            {
                                CL_TRY(code_obj->emit_jump_if_false(
                                    source_offset, skip_target));
                            }
                            std::swap(recv, prod);
                        }
                        CL_TRY(skip_target.resolve());

                        break;
                    }

                case AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY:
                    {
                        JumpTarget skip_target(code_obj);
                        CL_TRY(codegen_node(children[0]));
                        switch(kind.operator_kind)
                        {
                            case AstOperatorKind::SHORTCUTTING_AND:
                                CL_TRY(code_obj->emit_jump_if_false(
                                    source_offset, skip_target));
                                break;
                            case AstOperatorKind::SHORTCUTTING_OR:
                                CL_TRY(code_obj->emit_jump_if_true(
                                    source_offset, skip_target));
                                break;
                            default:
                                assert(0);
                                break;
                        }
                        CL_TRY(codegen_node(children[1]));
                        CL_TRY(skip_target.resolve());
                        break;
                    }

                case AstNodeKind::EXPRESSION_CALL:
                    CL_TRY(codegen_function_call(node_idx));
                    break;

                case AstNodeKind::EXPRESSION_ATTRIBUTE:
                    {
                        ScopedRegister receiver_reg =
                            CL_TRY(codegen_node_into_a_register(children[0]));
                        uint8_t constant_idx =
                            CL_TRY(code_obj->allocate_constant(
                                av.constants[node_idx]));
                        CL_TRY(code_obj->emit_load_attr(
                            source_offset, receiver_reg.reg, constant_idx));
                        break;
                    }

                case AstNodeKind::EXPRESSION_SLICE:
                    CL_TRY(codegen_slice_literal(node_idx));
                    break;

                case AstNodeKind::STATEMENT_SEQUENCE:
                case AstNodeKind::STATEMENT_EXPRESSION:
                    for(int32_t ch_idx: children)
                    {
                        CL_TRY(codegen_node(ch_idx));
                    }
                    break;

                case AstNodeKind::STATEMENT_IF:
                    {
                        JumpTarget done_target(code_obj);

                        for(size_t i = 0; i < children.size() - 1; i += 2)
                        {
                            JumpTarget next_target(code_obj);
                            CL_TRY(codegen_node(
                                children[i + 0]));  // condition, initial check
                            CL_TRY(code_obj->emit_jump_if_false(source_offset,
                                                                next_target));
                            CL_TRY(codegen_node(children[i + 1]));  // then

                            if(i + 2 != children.size())
                            {
                                // if we have more to emit, we have to generate
                                // a jump to the done target. otherwise, we'll
                                // just fall through
                                CL_TRY(code_obj->emit_jump(source_offset,
                                                           done_target));
                            }
                            CL_TRY(next_target.resolve());
                        }
                        if(children.size() & 1)  // odd -> else
                        {
                            CL_TRY(codegen_node(children.back()));  // else
                        }
                        CL_TRY(done_target.resolve());

                        break;
                    }

                case AstNodeKind::STATEMENT_WHILE:
                    {
                        JumpTarget loop_start_target(code_obj);
                        JumpTarget else_target(code_obj);
                        JumpTarget break_target(code_obj);
                        JumpTarget continue_target(code_obj);
                        CL_TRY(codegen_node(
                            children[0]));  // condition, initial check
                        CL_TRY(code_obj->emit_jump_if_false(source_offset,
                                                            else_target));

                        CL_TRY(loop_start_target.resolve());

                        loop_targets.emplace_back(&break_target,
                                                  &continue_target,
                                                  active_cleanups.size());
                        CL_TRY(codegen_node(children[1]));  // body
                        loop_targets.pop_back();

                        CL_TRY(continue_target.resolve());
                        CL_TRY(codegen_node(
                            children[0]));  // condition, non-initial check
                        CL_TRY(code_obj->emit_jump_if_true(source_offset,
                                                           loop_start_target));
                        CL_TRY(else_target.resolve());
                        if(children.size() == 3)
                        {
                            CL_TRY(codegen_node(
                                children[2]));  // else clause of a loop
                        }
                        CL_TRY(break_target.resolve());
                        break;
                    }

                case AstNodeKind::STATEMENT_FOR:
                    {
                        int32_t target_idx = children[0];
                        std::optional<uint8_t> range_call_arity =
                            direct_range_call_arity(children[1]);
                        if(range_call_arity.has_value())
                        {
                            CL_TRY(codegen_direct_range_for_loop(
                                node_idx, target_idx, *range_call_arity));
                            break;
                        }

                        int32_t iterable_idx = children[1];
                        int32_t body_idx = children[2];
                        int32_t else_idx =
                            children.size() == 4 ? children[3] : -1;
                        TemporaryReg iterator_reg(*code_obj);
                        JumpTarget else_target(code_obj);
                        JumpTarget break_target(code_obj);

                        uint8_t iter_constant_idx =
                            CL_TRY(code_obj->allocate_constant(
                                interned_string(L"__iter__")));
                        uint8_t not_iterable_type_constant_idx =
                            CL_TRY(code_obj->allocate_constant(Value::from_oop(
                                active_thread()->class_for_builtin_name(
                                    L"TypeError"))));
                        uint8_t not_iterable_message_constant_idx =
                            CL_TRY(code_obj->allocate_constant(
                                interned_string(L"object is not iterable")));
                        {
                            TemporaryReg call_args(
                                *code_obj, 1, RegisterAlignment::CallFrame);
                            CL_TRY(codegen_node_into_specific_register(
                                iterable_idx, call_args));
                            CL_TRY(code_obj->emit_call_special_method(
                                source_offset, call_args, iter_constant_idx, 0,
                                not_iterable_type_constant_idx,
                                not_iterable_message_constant_idx));
                        }
                        CL_TRY(
                            code_obj->emit_star(source_offset, iterator_reg));
                        CL_TRY(codegen_iterator_driven_for_loop(
                            source_offset, target_idx, body_idx, iterator_reg,
                            else_target, break_target));
                        CL_TRY(else_target.resolve());
                        if(else_idx >= 0)
                        {
                            CL_TRY(codegen_node(else_idx));
                        }
                        CL_TRY(break_target.resolve());
                        break;
                    }

                case AstNodeKind::STATEMENT_TRY:
                    CL_TRY(codegen_try_statement(node_idx));
                    break;

                case AstNodeKind::STATEMENT_WITH:
                    CL_TRY(codegen_with_statement(node_idx));
                    break;

                case AstNodeKind::STATEMENT_BREAK:
                    if(loop_targets.empty())
                    {
                        return Expected<void>::raise_exception(
                            L"SyntaxError", L"'break' outside loop");
                    }
                    else
                    {
                        CL_TRY(emit_active_cleanups_until_and_then(
                            loop_targets.back().cleanup_depth,
                            [&]() -> Expected<void> {
                                CL_TRY(code_obj->emit_jump(
                                    source_offset,
                                    *loop_targets.back().break_target));
                                return Expected<void>::ok();
                            }));
                    }
                    break;

                case AstNodeKind::STATEMENT_CONTINUE:
                    if(loop_targets.empty())
                    {
                        return Expected<void>::raise_exception(
                            L"SyntaxError", L"'continue' not properly in loop");
                    }
                    else
                    {
                        CL_TRY(emit_active_cleanups_until_and_then(
                            loop_targets.back().cleanup_depth,
                            [&]() -> Expected<void> {
                                CL_TRY(code_obj->emit_jump(
                                    source_offset,
                                    *loop_targets.back().continue_target));
                                return Expected<void>::ok();
                            }));
                    }
                    break;

                case AstNodeKind::STATEMENT_RETURN:
                    {
                        if(mode() != CodegenMode::Function)
                        {
                            return Expected<void>::raise_exception(
                                L"SyntaxError", L"'return' outside function");
                        }
                        if(!children.empty())
                        {
                            CL_TRY(codegen_node(children[0]));
                        }
                        else
                        {
                            CL_TRY(code_obj->emit_lda_none(source_offset));
                        }
                        if(!active_cleanups.empty())
                        {
                            TemporaryReg return_value(*code_obj);
                            CL_TRY(code_obj->emit_star(source_offset,
                                                       return_value));
                            CL_TRY(emit_active_cleanups_until_and_then(
                                0, [&]() -> Expected<void> {
                                    CL_TRY(code_obj->emit_ldar(source_offset,
                                                               return_value));
                                    CL_TRY(
                                        code_obj->emit_return(source_offset));
                                    return Expected<void>::ok();
                                }));
                            break;
                        }
                        CL_TRY(code_obj->emit_return(source_offset));
                        break;
                    }

                case AstNodeKind::STATEMENT_PASS:
                case AstNodeKind::STATEMENT_GLOBAL:
                case AstNodeKind::STATEMENT_NONLOCAL:
                    break;

                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                    CL_TRY(codegen_function_definition(node_idx));
                    break;

                case AstNodeKind::STATEMENT_CLASS_DEF:
                    CL_TRY(codegen_class_definition(node_idx));
                    break;

                case AstNodeKind::EXPRESSION_TUPLE:
                    CL_TRY(codegen_tuple_literal(node_idx));
                    break;

                case AstNodeKind::EXPRESSION_LIST:
                    CL_TRY(codegen_list_literal(node_idx));
                    break;

                case AstNodeKind::EXPRESSION_DICT:
                    CL_TRY(codegen_dict_literal(node_idx));
                    break;

                case AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT:
                    return Expected<void>::raise_exception(
                        L"SystemError",
                        L"should not end here - this is handled by "
                        L"EXPRESSION_COMPARISON");

                case AstNodeKind::STATEMENT_EXCEPT_HANDLER:
                case AstNodeKind::STATEMENT_ELSE_HANDLER:
                case AstNodeKind::STATEMENT_FINALLY_HANDLER:
                    return Expected<void>::raise_exception(
                        L"SystemError",
                        L"should not end here - this is handled by "
                        L"STATEMENT_TRY");

                case AstNodeKind::WITH_ITEM:
                case AstNodeKind::IMPORT_ALIAS:
                case AstNodeKind::IMPORT_STAR:
                    return Expected<void>::raise_exception(
                        L"SystemError",
                        L"should not end here - this is handled by the owning "
                        L"statement");

                case AstNodeKind::PARAMETER:
                case AstNodeKind::PARAMETER_VARARGS:
                case AstNodeKind::PARAMETER_KWARGS:
                case AstNodeKind::PARAMETER_SEQUENCE:
                case AstNodeKind::PARAMETER_SIGNATURE:
                    return Expected<void>::raise_exception(
                        L"SystemError",
                        L"should not end here - this is handled by function "
                        L"definitions");
            }
            return Expected<void>::ok();
        }
    };

    Expected<CodeObject *> AstCodegen::run_module()
    {
        if(body_idx < 0)
        {
            CL_TRY(code_obj->emit_lda_none(0));
            CL_TRY(code_obj->emit_return(0));
            CodeObject *result = CL_TRY(code_obj->finalize());
            return Expected<CodeObject *>::ok(incref(result));
        }
        if(av.children[body_idx].empty())
        {
            CL_TRY(code_obj->emit_lda_none(0));
            CL_TRY(code_obj->emit_return(0));
            CodeObject *result = CL_TRY(code_obj->finalize());
            return Expected<CodeObject *>::ok(incref(result));
        }

        if(result_mode == ModuleResultMode::Interactive)
        {
            AstChildren body_children = av.children[body_idx];
            if(body_children.size() == 1 &&
               av.kinds[body_children[0]].node_kind ==
                   AstNodeKind::STATEMENT_EXPRESSION)
            {
                AstChildren statement_children = av.children[body_children[0]];
                if(statement_children.size() == 1)
                {
                    CL_TRY(codegen_node(statement_children[0]));
                    CL_TRY(code_obj->emit_return(
                        av.source_offsets[statement_children[0]]));
                    CodeObject *result = CL_TRY(code_obj->finalize());
                    return Expected<CodeObject *>::ok(incref(result));
                }
            }

            CL_TRY(codegen_node(body_idx));
            CL_TRY(code_obj->emit_lda_none(0));
            CL_TRY(code_obj->emit_return(0));
            CodeObject *result = CL_TRY(code_obj->finalize());
            return Expected<CodeObject *>::ok(incref(result));
        }

        CL_TRY(codegen_node(body_idx));
        CL_TRY(code_obj->emit_return(0));
        CodeObject *result = CL_TRY(code_obj->finalize());
        return Expected<CodeObject *>::ok(incref(result));
    }

    bool has_varargs_parameter(const AstVector &av, AstChildren param_children)
    {
        for(int32_t param_idx: param_children)
        {
            if(av.kinds[param_idx].node_kind == AstNodeKind::PARAMETER_VARARGS)
            {
                return true;
            }
        }
        return false;
    }

    bool has_kwargs_parameter(const AstVector &av, AstChildren param_children)
    {
        for(int32_t param_idx: param_children)
        {
            if(av.kinds[param_idx].node_kind == AstNodeKind::PARAMETER_KWARGS)
            {
                return true;
            }
        }
        return false;
    }

    bool parameter_sequence_has_required_parameter(const AstVector &av,
                                                   AstChildren param_children)
    {
        for(int32_t param_idx: param_children)
        {
            if(av.kinds[param_idx].node_kind == AstNodeKind::PARAMETER &&
               av.children[param_idx].empty())
            {
                return true;
            }
        }
        return false;
    }

    Optional<TValue<String>> docstring_for_body(const AstVector &av,
                                                int32_t body_idx)
    {
        AstChildren body_children = av.children[body_idx];
        if(body_children.empty())
        {
            return Optional<TValue<String>>::none();
        }

        int32_t first_statement_idx = body_children[0];
        if(av.kinds[first_statement_idx].node_kind !=
           AstNodeKind::STATEMENT_EXPRESSION)
        {
            return Optional<TValue<String>>::none();
        }

        AstChildren statement_children = av.children[first_statement_idx];
        if(statement_children.size() != 1)
        {
            return Optional<TValue<String>>::none();
        }

        int32_t expression_idx = statement_children[0];
        AstKind expression_kind = av.kinds[expression_idx];
        if(expression_kind.node_kind != AstNodeKind::EXPRESSION_LITERAL ||
           expression_kind.operator_kind != AstOperatorKind::STRING)
        {
            return Optional<TValue<String>>::none();
        }

        return Optional<TValue<String>>::some(
            TValue<String>::from_value_assumed(av.constants[expression_idx]));
    }

    Expected<CodeObject *> codegen_function(const AstVector &av,
                                            ModuleObject *module,
                                            CodeObjectBuilder *parent_code_obj,
                                            int32_t node_idx,
                                            LanguageMode language_mode)
    {
        AstChildren children = av.children[node_idx];
        uint32_t source_offset = av.source_offsets[node_idx];
        AstChildren param_children =
            CL_TRY(supported_runtime_parameter_order(av, children[0]));
        Scope *local_scope =
            make_internal_raw<Scope>(parent_code_obj->local_scope());
        TValue<String> function_name =
            TValue<String>::from_value_assumed(av.constants[node_idx]);
        CodeObjectBuilder fun_obj(av.compilation_unit,
                                  TValue<ModuleObject>::from_oop(module),
                                  local_scope, function_name);

        fun_obj.set_docstring(docstring_for_body(av, children[1]));
        AstChildren posonly = parameter_signature_group(av, children[0], 0);
        AstChildren pos_or_kw = parameter_signature_group(av, children[0], 1);
        AstChildren kwonly = parameter_signature_group(av, children[0], 3);
        fun_obj.n_parameters() = param_children.size();
        fun_obj.function_signature().n_posonly_parameters = posonly.size();
        fun_obj.n_positional_parameters() = posonly.size() + pos_or_kw.size();
        fun_obj.function_signature().n_pos_or_kw_parameters = pos_or_kw.size();
        fun_obj.function_signature().n_kwonly_parameters = kwonly.size();
        fun_obj.function_signature().has_required_keyword_only_parameters =
            parameter_sequence_has_required_parameter(av, kwonly);
        assert(fun_obj.n_positional_parameters() <= UINT16_MAX);
        assert(fun_obj.n_parameters() <= UINT16_MAX);
        for(uint32_t pos_or_kw_idx = 0; pos_or_kw_idx < pos_or_kw.size();
            ++pos_or_kw_idx)
        {
            int32_t parameter_node_idx = pos_or_kw[pos_or_kw_idx];
            fun_obj.function_keyword_remap().add(
                ast_string_constant(av, parameter_node_idx),
                static_cast<uint16_t>(posonly.size() + pos_or_kw_idx));
        }
        uint32_t first_kwonly_idx =
            uint32_t(posonly.size() + pos_or_kw.size() +
                     (has_varargs_parameter(av, param_children) ? 1 : 0));
        for(uint32_t kwonly_idx = 0; kwonly_idx < kwonly.size(); ++kwonly_idx)
        {
            int32_t parameter_node_idx = kwonly[kwonly_idx];
            fun_obj.function_keyword_remap().add(
                ast_string_constant(av, parameter_node_idx),
                static_cast<uint16_t>(first_kwonly_idx + kwonly_idx));
        }
        if(has_varargs_parameter(av, param_children))
        {
            fun_obj.parameter_flags() |= FunctionParameterFlags::HasVarArgs;
        }
        if(has_kwargs_parameter(av, param_children))
        {
            fun_obj.parameter_flags() |= FunctionParameterFlags::HasKwArgs;
        }
        for(int32_t ch: param_children)
        {
            assert(av.kinds[ch].node_kind == AstNodeKind::PARAMETER ||
                   av.kinds[ch].node_kind == AstNodeKind::PARAMETER_VARARGS ||
                   av.kinds[ch].node_kind == AstNodeKind::PARAMETER_KWARGS);
            fun_obj.get_local_scope_ptr()->register_slot_index_for_write(
                ast_string_constant(av, ch));
        }
        reserve_parameter_padding_and_frame_header(&fun_obj);

        AstCodegen fun_builder = CL_TRY(
            AstCodegen::make(av, &fun_obj, CodegenMode::Function, language_mode,
                             children[1], param_children));
        return fun_builder.run_function_body(source_offset, children[1]);
    }

    Expected<CodeObject *> codegen_class(const AstVector &av,
                                         ModuleObject *module,
                                         CodeObjectBuilder *parent_code_obj,
                                         int32_t node_idx,
                                         LanguageMode language_mode)
    {
        AstChildren children = av.children[node_idx];
        uint32_t source_offset = av.source_offsets[node_idx];
        int32_t body_idx = children[1];
        Scope *local_scope =
            make_internal_raw<Scope>(parent_code_obj->local_scope());
        CodeObjectBuilder class_obj(av.compilation_unit,
                                    TValue<ModuleObject>::from_oop(module),
                                    local_scope, parent_code_obj->name());

        class_obj.n_parameters() = 2;
        class_obj.get_local_scope_ptr()->reserve_empty_slots(2);
        reserve_parameter_padding_and_frame_header(&class_obj);

        AstCodegen class_builder = CL_TRY(AstCodegen::make(
            av, &class_obj, CodegenMode::Class, language_mode, body_idx, {}));
        return class_builder.run_class_body(source_offset, body_idx);
    }

    Expected<CodeObject *> AstCodegen::run_function_body(uint32_t source_offset,
                                                         int32_t body_idx)
    {
        CL_TRY(emit_local_binding_prologue());
        CL_TRY(codegen_node(body_idx));
        // finally, emit return None just in case. as a future optimisation, we
        // could check that all return paths already have a return statement
        CL_TRY(code_obj->emit_lda_none(source_offset));
        CL_TRY(code_obj->emit_return(source_offset));
        return code_obj->finalize();
    }

    Expected<CodeObject *> AstCodegen::run_class_body(uint32_t source_offset,
                                                      int32_t body_idx)
    {
        CL_TRY(emit_local_binding_prologue());
        CL_TRY(codegen_node(body_idx));
        CL_TRY(code_obj->emit_build_class(source_offset));
        return code_obj->finalize();
    }

    Expected<CodeObject *>
    codegen_module_in_module(const AstVector &av, ModuleObject *module,
                             LanguageMode language_mode,
                             ModuleResultMode result_mode)
    {
        TValue<ModuleObject> defining_module =
            TValue<ModuleObject>::from_oop(module);
        CodeObjectBuilder module_obj(
            av.compilation_unit, defining_module, nullptr,
            TValue<String>::from_value_assumed(module->get_name_binding()));
        AstCodegen builder = CL_TRY(
            AstCodegen::make(av, &module_obj, CodegenMode::Module,
                             language_mode, av.root_node, {}, result_mode));
        return builder.run_module();
    }

    Expected<CodeObject *> codegen_module(const AstVector &av,
                                          TValue<String> module_name,
                                          LanguageMode language_mode)
    {
        ModuleObject *module = active_thread()->make_module_object(
            module_name, active_vm()->global_builtins_module().raw_value(),
            Value::None(), Value::None(), Value::None(), Value::None(),
            Value::not_present());
        return codegen_module_in_module(av, module, language_mode,
                                        ModuleResultMode::File);
    }

}  // namespace cl
