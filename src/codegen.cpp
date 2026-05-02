#include "codegen.h"
#include "ast.h"
#include "attr.h"
#include "code_object_builder.h"
#include "runtime_helpers.h"
#include "scope.h"
#include "tokenizer.h"
#include <fmt/core.h>
#include <optional>
#include <utility>

namespace cl
{

    struct OpTableEntry
    {
        constexpr OpTableEntry(Bytecode _standard = Bytecode::Invalid,
                               Bytecode _binary_acc_smi = Bytecode::Invalid)
            : standard(_standard), binary_acc_smi(_binary_acc_smi)
        {
        }

        Bytecode standard;
        Bytecode binary_acc_smi;
    };

    struct OpTable
    {
        OpTableEntry table[AstOperatorKindSize];
    };

    constexpr OpTable make_table()
    {
        OpTable t;

        t.table[size_t(AstOperatorKind::ADD)] =
            OpTableEntry(Bytecode::Add, Bytecode::AddSmi);
        t.table[size_t(AstOperatorKind::SUBTRACT)] =
            OpTableEntry(Bytecode::Sub, Bytecode::SubSmi);
        t.table[size_t(AstOperatorKind::MULTIPLY)] =
            OpTableEntry(Bytecode::Mul, Bytecode::MulSmi);
        t.table[size_t(AstOperatorKind::DIVIDE)] =
            OpTableEntry(Bytecode::Div, Bytecode::DivSmi);
        t.table[size_t(AstOperatorKind::INT_DIVIDE)] =
            OpTableEntry(Bytecode::IntDiv, Bytecode::IntDivSmi);
        t.table[size_t(AstOperatorKind::POWER)] =
            OpTableEntry(Bytecode::Pow, Bytecode::PowSmi);
        t.table[size_t(AstOperatorKind::LEFTSHIFT)] =
            OpTableEntry(Bytecode::LeftShift, Bytecode::LeftShiftSmi);
        t.table[size_t(AstOperatorKind::RIGHTSHIFT)] =
            OpTableEntry(Bytecode::RightShift, Bytecode::RightShiftSmi);
        t.table[size_t(AstOperatorKind::MODULO)] =
            OpTableEntry(Bytecode::Mod, Bytecode::ModSmi);
        t.table[size_t(AstOperatorKind::BITWISE_OR)] =
            OpTableEntry(Bytecode::BitwiseOr, Bytecode::BitwiseOrSmi);
        t.table[size_t(AstOperatorKind::BITWISE_AND)] =
            OpTableEntry(Bytecode::BitwiseAnd, Bytecode::BitwiseAndSmi);
        t.table[size_t(AstOperatorKind::BITWISE_XOR)] =
            OpTableEntry(Bytecode::BitwiseXor, Bytecode::BitwiseXorSmi);

        t.table[size_t(AstOperatorKind::EQUAL)] =
            OpTableEntry(Bytecode::TestEqual);
        t.table[size_t(AstOperatorKind::NOT_EQUAL)] =
            OpTableEntry(Bytecode::TestNotEqual);
        t.table[size_t(AstOperatorKind::LESS)] =
            OpTableEntry(Bytecode::TestLess);
        t.table[size_t(AstOperatorKind::LESS_EQUAL)] =
            OpTableEntry(Bytecode::TestLessEqual);
        t.table[size_t(AstOperatorKind::GREATER)] =
            OpTableEntry(Bytecode::TestGreater);
        t.table[size_t(AstOperatorKind::GREATER_EQUAL)] =
            OpTableEntry(Bytecode::TestGreaterEqual);
        t.table[size_t(AstOperatorKind::IS)] = OpTableEntry(Bytecode::TestIs);
        t.table[size_t(AstOperatorKind::IS_NOT)] =
            OpTableEntry(Bytecode::TestIsNot);
        t.table[size_t(AstOperatorKind::IN)] = OpTableEntry(Bytecode::TestIn);
        t.table[size_t(AstOperatorKind::NOT_IN)] =
            OpTableEntry(Bytecode::TestNotIn);

        t.table[size_t(AstOperatorKind::NOT)] = OpTableEntry(Bytecode::Not);
        t.table[size_t(AstOperatorKind::NEGATE)] =
            OpTableEntry(Bytecode::Negate);
        t.table[size_t(AstOperatorKind::PLUS)] = OpTableEntry(Bytecode::Plus);
        t.table[size_t(AstOperatorKind::BITWISE_NOT)] =
            OpTableEntry(Bytecode::BitwiseNot);

        return t;
    }

    CodeObject *codegen_function(const AstVector &av, Scope *module_scope,
                                 CodeObjectBuilder *parent_code_obj,
                                 int32_t node_idx);
    CodeObject *codegen_class(const AstVector &av, Scope *module_scope,
                              CodeObjectBuilder *parent_code_obj,
                              int32_t node_idx);

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

    class AstCodegen
    {
    public:
        using RegisterIndex = int32_t;

        enum class Mode
        {
            Module,
            Class,
            Function
        };

        AstCodegen(const AstVector &_av, Scope *_module_scope,
                   CodeObjectBuilder *_code_obj, Mode _mode, int32_t _body_idx,
                   AstChildren param_children)
            : av(_av), module_scope(_module_scope), code_obj(_code_obj),
              body_idx(_body_idx), analysis(_mode, _av.size()),
              temporary_reg(FrameHeaderSize), max_temporary_reg(FrameHeaderSize)
        {
            analysis =
                analyze_code_object(code_obj, _body_idx, _mode, param_children);
            if(_mode != Mode::Module)
            {
                temporary_reg = code_obj->get_local_scope_ptr()->size();
                max_temporary_reg = temporary_reg;
            }
        }

        CodeObject *run_module();
        CodeObject *run_function_body(uint32_t source_offset, int32_t body_idx);
        CodeObject *run_class_body(uint32_t source_offset, int32_t body_idx);

    private:
        struct LoopTargetSet
        {
            LoopTargetSet(JumpTarget *_break_target,
                          JumpTarget *_continue_target)
                : break_target(_break_target), continue_target(_continue_target)
            {
            }

            JumpTarget *break_target;
            JumpTarget *continue_target;
        };
        constexpr static OpTable operator_table = make_table();

        constexpr static OpTableEntry get_operator_entry(AstOperatorKind ok)
        {
            return operator_table.table[size_t(ok)];
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

            Value rhs = av.constants[rhs_idx].as_value();
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
        Scope *module_scope;

        enum class BindingScope
        {
            Local,
            Global
        };

        enum class Presence
        {
            Present,
            Missing,
            Maybe
        };

        struct BindingInfo
        {
            Value name = Value::None();
            BindingScope scope = BindingScope::Global;
            uint32_t local_slot_idx = 0;
            Presence initial_presence = Presence::Missing;
            bool needs_entry_clear = false;
        };

        struct NameAccessAnalysis
        {
            BindingScope scope = BindingScope::Global;
            Presence presence = Presence::Maybe;
            uint32_t slot_idx = 0;
        };

        struct ScopeAnalysis
        {
            Mode mode = Mode::Module;
            std::vector<BindingInfo> bindings;
            std::vector<std::optional<NameAccessAnalysis>> loads;
            std::vector<std::optional<NameAccessAnalysis>> stores;
            std::vector<std::optional<NameAccessAnalysis>> deletes;

            ScopeAnalysis(Mode _mode, size_t n_nodes)
                : mode(_mode), loads(n_nodes), stores(n_nodes), deletes(n_nodes)
            {
            }
        };

        struct FlowState
        {
            std::vector<Presence> local_presence;
            std::vector<bool> may_be_entry_value;
        };

        Mode mode() const { return analysis.mode; }

        CodeObjectBuilder *code_obj;
        int32_t body_idx;
        ScopeAnalysis analysis;
        uint32_t temporary_reg;
        uint32_t max_temporary_reg;
        std::vector<LoopTargetSet> loop_targets;

        static Presence merge_presence(Presence left, Presence right)
        {
            if(left == right)
            {
                return left;
            }
            return Presence::Maybe;
        }

        static Presence conservative_loop_entry_presence(Presence presence)
        {
            if(presence == Presence::Present)
            {
                return Presence::Maybe;
            }
            return presence;
        }

        int32_t find_binding_idx(const ScopeAnalysis &analysis,
                                 Value name) const
        {
            for(size_t idx = 0; idx < analysis.bindings.size(); ++idx)
            {
                if(analysis.bindings[idx].name == name)
                {
                    return int32_t(idx);
                }
            }
            return -1;
        }

        BindingInfo *find_binding(ScopeAnalysis &analysis, Value name)
        {
            int32_t idx = find_binding_idx(analysis, name);
            if(idx < 0)
            {
                return nullptr;
            }
            return &analysis.bindings[idx];
        }

        const BindingInfo *find_binding(const ScopeAnalysis &analysis,
                                        Value name) const
        {
            int32_t idx = find_binding_idx(analysis, name);
            if(idx < 0)
            {
                return nullptr;
            }
            return &analysis.bindings[idx];
        }

        BindingInfo &
        ensure_local_binding(CodeObjectBuilder *target_code_obj,
                             ScopeAnalysis &analysis, Value name,
                             Presence initial_presence = Presence::Missing)
        {
            if(BindingInfo *binding = find_binding(analysis, name))
            {
                if(initial_presence == Presence::Present)
                {
                    binding->initial_presence = Presence::Present;
                }
                return *binding;
            }

            uint32_t slot_idx =
                target_code_obj->get_local_scope_ptr()
                    ->register_slot_index_for_write(TValue<String>(name));
            analysis.bindings.push_back(BindingInfo{
                name, BindingScope::Local, slot_idx, initial_presence, false});
            return analysis.bindings.back();
        }

        BindingInfo binding_for_name(const ScopeAnalysis &analysis,
                                     Value name) const
        {
            if(const BindingInfo *binding = find_binding(analysis, name))
            {
                return *binding;
            }
            return BindingInfo{name, BindingScope::Global, 0, Presence::Maybe,
                               false};
        }

        NameAccessAnalysis make_access(CodeObjectBuilder *target_code_obj,
                                       ScopeAnalysis &analysis, Value name,
                                       const FlowState &state, bool is_read,
                                       bool checks_presence)
        {
            BindingInfo binding = binding_for_name(analysis, name);
            if(binding.scope == BindingScope::Local)
            {
                int32_t binding_idx = find_binding_idx(analysis, name);
                assert(binding_idx >= 0);
                if(checks_presence &&
                   state.local_presence[size_t(binding_idx)] !=
                       Presence::Present &&
                   state.may_be_entry_value[size_t(binding_idx)])
                {
                    analysis.bindings[size_t(binding_idx)].needs_entry_clear =
                        true;
                }
                return NameAccessAnalysis{
                    BindingScope::Local,
                    state.local_presence[size_t(binding_idx)],
                    binding.local_slot_idx};
            }

            uint32_t slot_idx =
                is_read
                    ? target_code_obj->module_scope()
                          ->register_slot_index_for_read(TValue<String>(name))
                    : target_code_obj->module_scope()
                          ->register_slot_index_for_write(TValue<String>(name));
            return NameAccessAnalysis{BindingScope::Global, Presence::Maybe,
                                      slot_idx};
        }

        void mark_local_presence(ScopeAnalysis &analysis, FlowState &state,
                                 Value name, Presence presence)
        {
            int32_t binding_idx = find_binding_idx(analysis, name);
            if(binding_idx < 0 ||
               analysis.bindings[size_t(binding_idx)].scope !=
                   BindingScope::Local)
            {
                return;
            }
            state.local_presence[size_t(binding_idx)] = presence;
            state.may_be_entry_value[size_t(binding_idx)] = false;
        }

        void collect_code_object_bindings(CodeObjectBuilder *target_code_obj,
                                          ScopeAnalysis &analysis,
                                          int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];

            switch(kind.node_kind)
            {
                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                case AstNodeKind::STATEMENT_CLASS_DEF:
                    if(analysis.mode != Mode::Module)
                    {
                        ensure_local_binding(target_code_obj, analysis,
                                             av.constants[node_idx].as_value());
                    }
                    return;

                case AstNodeKind::STATEMENT_ASSIGN:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    {
                        int32_t lhs_idx = children[0];
                        if(analysis.mode != Mode::Module &&
                           av.kinds[lhs_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            ensure_local_binding(
                                target_code_obj, analysis,
                                av.constants[lhs_idx].as_value());
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_DEL:
                    if(analysis.mode != Mode::Module)
                    {
                        for(int32_t target_idx: children)
                        {
                            if(av.kinds[target_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                ensure_local_binding(
                                    target_code_obj, analysis,
                                    av.constants[target_idx].as_value());
                            }
                        }
                    }
                    break;

                case AstNodeKind::STATEMENT_FOR:
                    if(analysis.mode != Mode::Module)
                    {
                        int32_t target_idx = children[0];
                        if(av.kinds[target_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            ensure_local_binding(
                                target_code_obj, analysis,
                                av.constants[target_idx].as_value());
                        }
                    }
                    break;

                default:
                    break;
            }

            for(int32_t child_idx: children)
            {
                collect_code_object_bindings(target_code_obj, analysis,
                                             child_idx);
            }
        }

        void collect_modified_locals(const ScopeAnalysis &analysis,
                                     int32_t node_idx,
                                     std::vector<bool> &modified) const
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];

            auto mark_name = [&](Value name) {
                int32_t binding_idx = find_binding_idx(analysis, name);
                if(binding_idx >= 0 &&
                   analysis.bindings[size_t(binding_idx)].scope ==
                       BindingScope::Local)
                {
                    modified[size_t(binding_idx)] = true;
                }
            };

            switch(kind.node_kind)
            {
                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                case AstNodeKind::STATEMENT_CLASS_DEF:
                    mark_name(av.constants[node_idx].as_value());
                    return;

                case AstNodeKind::STATEMENT_ASSIGN:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    {
                        int32_t lhs_idx = children[0];
                        if(av.kinds[lhs_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            mark_name(av.constants[lhs_idx].as_value());
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_DEL:
                    for(int32_t target_idx: children)
                    {
                        if(av.kinds[target_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            mark_name(av.constants[target_idx].as_value());
                        }
                    }
                    break;

                case AstNodeKind::STATEMENT_FOR:
                    if(av.kinds[children[0]].node_kind ==
                       AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                    {
                        mark_name(av.constants[children[0]].as_value());
                    }
                    break;

                default:
                    break;
            }

            for(int32_t child_idx: children)
            {
                collect_modified_locals(analysis, child_idx, modified);
            }
        }

        FlowState initial_flow_state(const ScopeAnalysis &analysis) const
        {
            FlowState state;
            state.local_presence.reserve(analysis.bindings.size());
            state.may_be_entry_value.reserve(analysis.bindings.size());
            for(const BindingInfo &binding: analysis.bindings)
            {
                state.local_presence.push_back(binding.initial_presence);
                state.may_be_entry_value.push_back(binding.initial_presence !=
                                                   Presence::Present);
            }
            return state;
        }

        FlowState merge_flow_states(const FlowState &left,
                                    const FlowState &right) const
        {
            assert(left.local_presence.size() == right.local_presence.size());
            assert(left.may_be_entry_value.size() ==
                   right.may_be_entry_value.size());
            FlowState merged;
            merged.local_presence.reserve(left.local_presence.size());
            merged.may_be_entry_value.reserve(left.may_be_entry_value.size());
            for(size_t idx = 0; idx < left.local_presence.size(); ++idx)
            {
                merged.local_presence.push_back(merge_presence(
                    left.local_presence[idx], right.local_presence[idx]));
                merged.may_be_entry_value.push_back(
                    left.may_be_entry_value[idx] ||
                    right.may_be_entry_value[idx]);
            }
            return merged;
        }

        void analyze_flow_node(CodeObjectBuilder *target_code_obj,
                               ScopeAnalysis &analysis, int32_t node_idx,
                               FlowState &state)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];

            auto annotate_read = [&](int32_t read_idx) {
                analysis.loads[read_idx] = make_access(
                    target_code_obj, analysis,
                    av.constants[read_idx].as_value(), state, true, true);
            };
            auto annotate_write = [&](int32_t write_idx) {
                analysis.stores[write_idx] = make_access(
                    target_code_obj, analysis,
                    av.constants[write_idx].as_value(), state, false, false);
                mark_local_presence(analysis, state,
                                    av.constants[write_idx].as_value(),
                                    Presence::Present);
            };
            auto annotate_delete = [&](int32_t delete_idx) {
                analysis.deletes[delete_idx] = make_access(
                    target_code_obj, analysis,
                    av.constants[delete_idx].as_value(), state, false, true);
                mark_local_presence(analysis, state,
                                    av.constants[delete_idx].as_value(),
                                    Presence::Missing);
            };
            auto annotate_named_definition = [&](int32_t definition_idx) {
                analysis.stores[definition_idx] =
                    make_access(target_code_obj, analysis,
                                av.constants[definition_idx].as_value(), state,
                                false, false);
                mark_local_presence(analysis, state,
                                    av.constants[definition_idx].as_value(),
                                    Presence::Present);
            };

            switch(kind.node_kind)
            {
                case AstNodeKind::EXPRESSION_VARIABLE_REFERENCE:
                    annotate_read(node_idx);
                    break;

                case AstNodeKind::STATEMENT_ASSIGN:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    {
                        int32_t lhs_idx = children[0];
                        AstNodeKind lhs_kind = av.kinds[lhs_idx].node_kind;
                        if(lhs_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            if(kind.operator_kind != AstOperatorKind::NOP)
                            {
                                annotate_read(lhs_idx);
                            }
                            analyze_flow_node(target_code_obj, analysis,
                                              children[1], state);
                            annotate_write(lhs_idx);
                            break;
                        }

                        analyze_flow_node(target_code_obj, analysis, lhs_idx,
                                          state);
                        analyze_flow_node(target_code_obj, analysis,
                                          children[1], state);
                        break;
                    }

                case AstNodeKind::STATEMENT_DEL:
                    for(int32_t target_idx: children)
                    {
                        if(av.kinds[target_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            annotate_delete(target_idx);
                        }
                        else
                        {
                            analyze_flow_node(target_code_obj, analysis,
                                              target_idx, state);
                        }
                    }
                    break;

                case AstNodeKind::STATEMENT_SEQUENCE:
                case AstNodeKind::STATEMENT_EXPRESSION:
                    for(int32_t child_idx: children)
                    {
                        analyze_flow_node(target_code_obj, analysis, child_idx,
                                          state);
                    }
                    break;

                case AstNodeKind::STATEMENT_IF:
                    {
                        FlowState fallthrough = state;
                        std::optional<FlowState> merged_branches;
                        for(size_t idx = 0; idx + 1 < children.size(); idx += 2)
                        {
                            analyze_flow_node(target_code_obj, analysis,
                                              children[idx], fallthrough);
                            FlowState branch_state = fallthrough;
                            analyze_flow_node(target_code_obj, analysis,
                                              children[idx + 1], branch_state);
                            merged_branches =
                                merged_branches.has_value()
                                    ? merge_flow_states(*merged_branches,
                                                        branch_state)
                                    : branch_state;
                        }
                        if(children.size() & 1)
                        {
                            analyze_flow_node(target_code_obj, analysis,
                                              children.back(), fallthrough);
                        }
                        if(merged_branches.has_value())
                        {
                            state = merge_flow_states(*merged_branches,
                                                      fallthrough);
                        }
                        else
                        {
                            state = fallthrough;
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_WHILE:
                    {
                        analyze_flow_node(target_code_obj, analysis,
                                          children[0], state);

                        std::vector<bool> modified(analysis.bindings.size(),
                                                   false);
                        collect_modified_locals(analysis, children[1],
                                                modified);
                        FlowState body_state = state;
                        for(size_t idx = 0; idx < modified.size(); ++idx)
                        {
                            if(modified[idx])
                            {
                                body_state.local_presence[idx] =
                                    conservative_loop_entry_presence(
                                        body_state.local_presence[idx]);
                            }
                        }
                        analyze_flow_node(target_code_obj, analysis,
                                          children[1], body_state);

                        for(size_t idx = 0; idx < modified.size(); ++idx)
                        {
                            if(modified[idx])
                            {
                                state.local_presence[idx] = Presence::Maybe;
                                state.may_be_entry_value[idx] =
                                    state.may_be_entry_value[idx] ||
                                    body_state.may_be_entry_value[idx];
                            }
                        }
                        if(children.size() == 3)
                        {
                            analyze_flow_node(target_code_obj, analysis,
                                              children[2], state);
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_FOR:
                    {
                        int32_t target_idx = children[0];
                        int32_t iterable_idx = children[1];
                        int32_t body_idx = children[2];
                        analyze_flow_node(target_code_obj, analysis,
                                          iterable_idx, state);

                        std::vector<bool> modified(analysis.bindings.size(),
                                                   false);
                        collect_modified_locals(analysis, body_idx, modified);
                        if(av.kinds[target_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            int32_t target_binding_idx = find_binding_idx(
                                analysis, av.constants[target_idx].as_value());
                            if(target_binding_idx >= 0)
                            {
                                modified[size_t(target_binding_idx)] = true;
                            }
                        }

                        FlowState body_state = state;
                        for(size_t idx = 0; idx < modified.size(); ++idx)
                        {
                            if(modified[idx])
                            {
                                body_state.local_presence[idx] =
                                    conservative_loop_entry_presence(
                                        body_state.local_presence[idx]);
                            }
                        }
                        if(av.kinds[target_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            analysis.stores[target_idx] =
                                make_access(target_code_obj, analysis,
                                            av.constants[target_idx].as_value(),
                                            body_state, false, false);
                            mark_local_presence(
                                analysis, body_state,
                                av.constants[target_idx].as_value(),
                                Presence::Present);
                        }
                        analyze_flow_node(target_code_obj, analysis, body_idx,
                                          body_state);

                        for(size_t idx = 0; idx < modified.size(); ++idx)
                        {
                            if(modified[idx])
                            {
                                state.local_presence[idx] = Presence::Maybe;
                                state.may_be_entry_value[idx] =
                                    state.may_be_entry_value[idx] ||
                                    body_state.may_be_entry_value[idx];
                            }
                        }

                        if(children.size() == 4)
                        {
                            analyze_flow_node(target_code_obj, analysis,
                                              children[3], state);
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                    {
                        AstChildren param_children = av.children[children[0]];
                        for(int32_t param_idx: param_children)
                        {
                            for(int32_t default_idx: av.children[param_idx])
                            {
                                analyze_flow_node(target_code_obj, analysis,
                                                  default_idx, state);
                            }
                        }
                        annotate_named_definition(node_idx);
                        break;
                    }

                case AstNodeKind::STATEMENT_CLASS_DEF:
                    for(int32_t base_idx: av.children[children[0]])
                    {
                        analyze_flow_node(target_code_obj, analysis, base_idx,
                                          state);
                    }
                    annotate_named_definition(node_idx);
                    break;

                case AstNodeKind::STATEMENT_RETURN:
                    if(!children.empty())
                    {
                        analyze_flow_node(target_code_obj, analysis,
                                          children[0], state);
                    }
                    break;

                case AstNodeKind::STATEMENT_BREAK:
                case AstNodeKind::STATEMENT_CONTINUE:
                case AstNodeKind::STATEMENT_PASS:
                case AstNodeKind::STATEMENT_GLOBAL:
                case AstNodeKind::STATEMENT_NONLOCAL:
                    break;

                default:
                    for(int32_t child_idx: children)
                    {
                        analyze_flow_node(target_code_obj, analysis, child_idx,
                                          state);
                    }
                    break;
            }
        }

        ScopeAnalysis analyze_code_object(CodeObjectBuilder *target_code_obj,
                                          int32_t body_idx, Mode mode,
                                          AstChildren param_children = {})
        {
            ScopeAnalysis analysis(mode, av.size());
            if(mode == Mode::Function)
            {
                for(int32_t param_idx: param_children)
                {
                    ensure_local_binding(target_code_obj, analysis,
                                         av.constants[param_idx].as_value(),
                                         Presence::Present);
                }
            }
            collect_code_object_bindings(target_code_obj, analysis, body_idx);
            FlowState state = initial_flow_state(analysis);
            analyze_flow_node(target_code_obj, analysis, body_idx, state);
            return analysis;
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

        void emit_local_binding_prologue()
        {
            for(const BindingInfo &binding: analysis.bindings)
            {
                if(binding.scope == BindingScope::Local &&
                   binding.needs_entry_clear)
                {
                    code_obj->emit_clear_local(0, binding.local_slot_idx);
                }
            }
        }

        void emit_variable_load(uint32_t source_offset, int32_t node_idx)
        {
            const NameAccessAnalysis &access = load_access(node_idx);
            switch(access.scope)
            {
                case BindingScope::Local:
                    if(access.presence == Presence::Present)
                    {
                        code_obj->emit_ldar(source_offset, access.slot_idx);
                    }
                    else
                    {
                        code_obj->emit_load_local_checked(source_offset,
                                                          access.slot_idx);
                    }
                    break;
                case BindingScope::Global:
                    code_obj->emit_lda_global(source_offset, access.slot_idx);
                    break;
            }
        }

        void emit_variable_store(uint32_t source_offset, int32_t node_idx)
        {
            const NameAccessAnalysis &access = store_access(node_idx);
            switch(access.scope)
            {
                case BindingScope::Local:
                    code_obj->emit_star(source_offset, access.slot_idx);
                    break;
                case BindingScope::Global:
                    code_obj->emit_sta_global(source_offset, access.slot_idx);
                    break;
            }
        }

        void emit_variable_delete(uint32_t source_offset, int32_t node_idx)
        {
            const NameAccessAnalysis &access = delete_access(node_idx);
            switch(access.scope)
            {
                case BindingScope::Local:
                    code_obj->emit_del_local(source_offset, access.slot_idx);
                    break;
                case BindingScope::Global:
                    code_obj->emit_del_global(source_offset, access.slot_idx);
                    break;
            }
        }

        class TemporaryReg
        {
        public:
            TemporaryReg(AstCodegen *_builder, uint32_t _n_regs = 1)
                : builder(_builder), n_regs(_n_regs)
            {
                reg = builder->temporary_reg;
                builder->temporary_reg += n_regs;
                builder->max_temporary_reg = std::max(
                    builder->max_temporary_reg, builder->temporary_reg);
            }

            TemporaryReg(const TemporaryReg &) = delete;
            TemporaryReg &operator=(const TemporaryReg &) = delete;

            TemporaryReg(TemporaryReg &&other) noexcept
                : builder(other.builder), n_regs(other.n_regs), reg(other.reg)
            {
                other.builder = nullptr;
                other.n_regs = 0;
                other.reg = 0;
            }

            TemporaryReg &operator=(TemporaryReg &&other) = delete;

            ~TemporaryReg()
            {
                if(builder == nullptr)
                {
                    return;
                }
                builder->temporary_reg -= n_regs;
                assert(reg == builder->temporary_reg);
            }

            operator uint32_t() const { return reg; }

        private:
            AstCodegen *builder;
            uint32_t n_regs;
            uint32_t reg;
        };

        struct ScopedRegister
        {
            RegisterIndex reg;
            std::optional<TemporaryReg> temp;
        };

        static constexpr AstKind NumericalConstant =
            AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NUMBER);

        ScopedRegister codegen_node_to_register(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            if(kind.node_kind == AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                const NameAccessAnalysis &access = load_access(node_idx);
                if(access.scope == BindingScope::Local &&
                   access.presence == Presence::Present)
                {
                    return {RegisterIndex(access.slot_idx), std::nullopt};
                }
            }

            uint32_t source_offset = av.source_offsets[node_idx];
            codegen_node(node_idx);
            std::optional<TemporaryReg> temp;
            temp.emplace(this);
            code_obj->emit_star(source_offset, RegisterIndex(*temp));
            return {RegisterIndex(*temp), std::move(temp)};
        }

        // used for both regular binary expressions and augmented assignment, so
        // pull out
        void codegen_binary_expression(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            if(immediate.has_value())
            {
                codegen_node(children[0]);
                code_obj->emit_binary_smi_op(source_offset,
                                             entry.binary_acc_smi, *immediate);
            }
            else
            {
                ScopedRegister lhs_reg = codegen_node_to_register(children[0]);
                codegen_node(children[1]);
                code_obj->emit_binary_op(source_offset, entry.standard,
                                         lhs_reg.reg);
            }
        }

        void codegen_comparison_fragment(int32_t node_idx, int32_t recv,
                                         int32_t prod)
        {
            AstKind kind = av.kinds[node_idx];
            assert(kind.node_kind ==
                   AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT);
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            OpTableEntry entry = get_operator_entry(kind.operator_kind);

            codegen_node(children[0]);
            if(prod >= 0)
            {
                code_obj->emit_star(source_offset, prod);
            }
            code_obj->emit_compare_op(source_offset, entry.standard, recv);
        }

        void codegen_function_definition(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            AstChildren param_children = av.children[children[0]];
            CodeObject *fun_obj =
                codegen_function(av, module_scope, code_obj, node_idx);

            // stick this code object into the constant table, load it, and call
            // the
            uint32_t constant_idx =
                code_obj->allocate_constant(Value::from_oop(fun_obj));
            uint32_t n_defaults = count_default_parameters(param_children);
            if(n_defaults == 0)
            {
                code_obj->emit_create_function(source_offset, constant_idx);
            }
            else
            {
                TemporaryReg default_values(this, n_defaults);
                size_t first_default_idx =
                    fun_obj->n_positional_parameters - n_defaults;
                for(size_t i = 0; i < n_defaults; ++i)
                {
                    int32_t param_idx = param_children[first_default_idx + i];
                    AstChildren default_children = av.children[param_idx];
                    assert(default_children.size() == 1);
                    codegen_node(default_children[0]);
                    code_obj->emit_star(source_offset, default_values + i);
                }
                code_obj->emit_create_tuple(source_offset, default_values,
                                            n_defaults);

                TemporaryReg default_tuple(this);
                code_obj->emit_star(source_offset, default_tuple);
                code_obj->emit_create_function_with_defaults(
                    source_offset, constant_idx, default_tuple);
            }

            emit_variable_store(source_offset, node_idx);
        }

        uint32_t count_default_parameters(AstChildren param_children) const
        {
            uint32_t n_defaults = 0;
            for(int32_t param_idx: param_children)
            {
                if(av.kinds[param_idx].node_kind != AstNodeKind::PARAMETER)
                {
                    continue;
                }
                if(!av.children[param_idx].empty())
                {
                    ++n_defaults;
                }
            }
            return n_defaults;
        }

        void codegen_class_definition(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t bases_idx = children[0];
            CodeObject *class_obj =
                codegen_class(av, module_scope, code_obj, node_idx);

            uint32_t body_constant_idx =
                code_obj->allocate_constant(Value::from_oop(class_obj));
            AstChildren bases = av.children[bases_idx];
            uint32_t name_constant_idx =
                code_obj->allocate_constant(av.constants[node_idx]);
            code_obj->emit_lda_constant(source_offset, name_constant_idx);
            code_obj->emit_star(source_offset, OutgoingArgReg(0));

            TemporaryReg base_regs(this, std::max<size_t>(bases.size(), 1));
            if(bases.empty())
            {
                uint32_t object_constant_idx = code_obj->allocate_constant(
                    Value::from_oop(active_vm()->object_class()));
                code_obj->emit_lda_constant(source_offset, object_constant_idx);
                code_obj->emit_star(source_offset, base_regs);
            }
            else
            {
                for(size_t i = 0; i < bases.size(); ++i)
                {
                    codegen_node(bases[i]);
                    code_obj->emit_star(source_offset, base_regs + i);
                }
            }
            code_obj->emit_create_tuple(source_offset, base_regs,
                                        std::max<size_t>(bases.size(), 1));
            code_obj->emit_star(source_offset, OutgoingArgReg(1));

            code_obj->emit_create_class(source_offset, body_constant_idx,
                                        OutgoingArgReg(0));

            emit_variable_store(source_offset, node_idx);
        }

        void codegen_function_call(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            AstChildren args = av.children[children[1]];

            if(av.kinds[children[0]].node_kind ==
               AstNodeKind::EXPRESSION_ATTRIBUTE)
            {
                AstChildren method_children = av.children[children[0]];
                uint8_t constant_idx =
                    code_obj->allocate_constant(av.constants[children[0]]);
                codegen_node(method_children[0]);
                code_obj->emit_star(source_offset, OutgoingArgReg(0));

                for(size_t i = 0; i < args.size(); ++i)
                {
                    codegen_node(args[i]);
                    code_obj->emit_star(source_offset, OutgoingArgReg(1 + i));
                }
                uint8_t read_cache_idx =
                    code_obj->allocate_attribute_read_cache();
                uint8_t call_cache_idx =
                    code_obj->allocate_function_call_cache();
                code_obj->emit_call_method_attr(
                    source_offset, OutgoingArgReg(0), constant_idx,
                    read_cache_idx, call_cache_idx, args.size());
                return;
            }

            // function itself
            TemporaryReg callable_reg(this);
            codegen_node(children[0]);
            code_obj->emit_star(source_offset, callable_reg);

            for(size_t i = 0; i < args.size(); ++i)
            {
                codegen_node(args[i]);
                code_obj->emit_star(source_offset, OutgoingArgReg(i));
            }
            code_obj->emit_call_simple(source_offset, callable_reg,
                                       OutgoingArgReg(0), args.size());
        }

        void codegen_list_literal(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(this, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                codegen_node(children[i]);
                code_obj->emit_star(source_offset, regs + i);
            }
            code_obj->emit_create_list(source_offset, regs, children.size());
        }

        void codegen_tuple_literal(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(this, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                codegen_node(children[i]);
                code_obj->emit_star(source_offset, regs + i);
            }
            code_obj->emit_create_tuple(source_offset, regs, children.size());
        }

        void codegen_dict_literal(int32_t node_idx)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(this, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                codegen_node(children[i]);
                code_obj->emit_star(source_offset, regs + i);
            }
            code_obj->emit_create_dict(source_offset, regs,
                                       children.size() / 2);
        }

        void codegen_subscript_assignment(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t lhs_idx = children[0];
            AstChildren lhs_children = av.children[lhs_idx];
            ScopedRegister receiver_reg =
                codegen_node_to_register(lhs_children[0]);
            ScopedRegister key_reg = codegen_node_to_register(lhs_children[1]);

            if(kind.operator_kind == AstOperatorKind::NOP)
            {
                codegen_node(children[1]);
                code_obj->emit_store_subscript(source_offset, receiver_reg.reg,
                                               key_reg.reg);
                return;
            }

            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            code_obj->emit_ldar(source_offset, key_reg.reg);
            code_obj->emit_load_subscript(source_offset, receiver_reg.reg);

            if(immediate.has_value())
            {
                code_obj->emit_binary_smi_op(source_offset,
                                             entry.binary_acc_smi, *immediate);
            }
            else
            {
                TemporaryReg lhs_value_reg(this);
                code_obj->emit_star(source_offset, lhs_value_reg);
                codegen_node(children[1]);
                code_obj->emit_binary_op(source_offset, entry.standard,
                                         lhs_value_reg);
            }

            code_obj->emit_store_subscript(source_offset, receiver_reg.reg,
                                           key_reg.reg);
        }

        void codegen_attribute_assignment(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t lhs_idx = children[0];
            AstChildren lhs_children = av.children[lhs_idx];
            uint8_t constant_idx =
                code_obj->allocate_constant(av.constants[lhs_idx]);
            ScopedRegister receiver_reg =
                codegen_node_to_register(lhs_children[0]);

            if(kind.operator_kind == AstOperatorKind::NOP)
            {
                codegen_node(children[1]);
                uint8_t cache_idx =
                    code_obj->allocate_attribute_mutation_cache();
                code_obj->emit_store_attr(source_offset, receiver_reg.reg,
                                          constant_idx, cache_idx);
                return;
            }

            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            uint8_t load_cache_idx = code_obj->allocate_attribute_read_cache();
            code_obj->emit_load_attr(source_offset, receiver_reg.reg,
                                     constant_idx, load_cache_idx);

            if(immediate.has_value())
            {
                code_obj->emit_binary_smi_op(source_offset,
                                             entry.binary_acc_smi, *immediate);
            }
            else
            {
                TemporaryReg lhs_value_reg(this);
                code_obj->emit_star(source_offset, lhs_value_reg);
                codegen_node(children[1]);
                code_obj->emit_binary_op(source_offset, entry.standard,
                                         lhs_value_reg);
            }

            uint8_t store_cache_idx =
                code_obj->allocate_attribute_mutation_cache();
            code_obj->emit_store_attr(source_offset, receiver_reg.reg,
                                      constant_idx, store_cache_idx);
        }

        void codegen_attribute_target_delete(uint32_t source_offset,
                                             int32_t target_idx)
        {
            AstChildren target_children = av.children[target_idx];
            uint8_t constant_idx =
                code_obj->allocate_constant(av.constants[target_idx]);
            ScopedRegister receiver_reg =
                codegen_node_to_register(target_children[0]);
            uint8_t cache_idx = code_obj->allocate_attribute_mutation_cache();
            code_obj->emit_del_attr(source_offset, receiver_reg.reg,
                                    constant_idx, cache_idx);
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
            if(av.constants[callable_idx] != Value(range_name))
            {
                return std::nullopt;
            }

            size_t n_args = av.children[children[1]].size();
            if(n_args < 1 || n_args > 3)
            {
                return std::nullopt;
            }

            return uint8_t(n_args);
        }

        void codegen_loop_body(int32_t body_idx, JumpTarget &break_target,
                               JumpTarget &continue_target)
        {
            loop_targets.emplace_back(&break_target, &continue_target);
            codegen_node(body_idx);
            loop_targets.pop_back();
        }

        void codegen_iterator_driven_for_loop(uint32_t source_offset,
                                              int32_t target_idx,
                                              int32_t body_idx,
                                              uint32_t iterator_reg,
                                              JumpTarget &else_target,
                                              JumpTarget &break_target)
        {
            JumpTarget loop_start_target(code_obj);
            JumpTarget continue_target(code_obj);

            loop_start_target.resolve();
            code_obj->emit_for_iter(source_offset, iterator_reg, else_target);
            emit_variable_store(source_offset, target_idx);

            codegen_loop_body(body_idx, break_target, continue_target);

            continue_target.resolve();
            code_obj->emit_jump(source_offset, loop_start_target);
        }

        void codegen_direct_range_for_loop(int32_t node_idx, int32_t target_idx,
                                           uint8_t n_args)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t iterable_idx = children[1];
            int32_t body_idx = children[2];
            int32_t else_idx = children.size() == 4 ? children[3] : -1;
            AstChildren call_children = av.children[iterable_idx];
            AstChildren args = av.children[call_children[1]];

            TemporaryReg range_regs(this, 1 + n_args);
            TemporaryReg iterator_reg(this);
            JumpTarget generic_fallback_target(code_obj);
            JumpTarget fast_loop_start_target(code_obj);
            JumpTarget fast_continue_target(code_obj);
            JumpTarget else_target(code_obj);
            JumpTarget break_target(code_obj);

            codegen_node(call_children[0]);
            code_obj->emit_star(source_offset, range_regs + 0);
            for(size_t i = 0; i < args.size(); ++i)
            {
                codegen_node(args[i]);
                code_obj->emit_star(source_offset, range_regs + 1 + i);
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

            code_obj->emit_for_prep_range(source_offset, prep_opcode,
                                          range_regs, generic_fallback_target);

            fast_loop_start_target.resolve();
            code_obj->emit_for_iter_range(source_offset, iter_opcode,
                                          range_regs, else_target);
            emit_variable_store(source_offset, target_idx);
            codegen_loop_body(body_idx, break_target, fast_continue_target);
            fast_continue_target.resolve();
            code_obj->emit_jump(source_offset, fast_loop_start_target);

            generic_fallback_target.resolve();
            for(size_t i = 0; i < args.size(); ++i)
            {
                code_obj->emit_ldar(source_offset, range_regs + 1 + i);
                code_obj->emit_star(source_offset, OutgoingArgReg(i));
            }
            code_obj->emit_call_simple(source_offset, range_regs,
                                       OutgoingArgReg(0), n_args);
            code_obj->emit_get_iter(source_offset);
            code_obj->emit_star(source_offset, iterator_reg);
            codegen_iterator_driven_for_loop(source_offset, target_idx,
                                             body_idx, iterator_reg,
                                             else_target, break_target);

            else_target.resolve();
            if(else_idx >= 0)
            {
                codegen_node(else_idx);
            }
            break_target.resolve();
        }

        void codegen_node(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            switch(kind.node_kind)
            {

                case AstNodeKind::EXPRESSION_VARIABLE_REFERENCE:
                    emit_variable_load(source_offset, node_idx);
                    break;

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
                            throw std::runtime_error(
                                "We don't support assignment to anything but "
                                "simple variables, attributes, and subscripts "
                                "yet");
                        }

                        if(lhs_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                        {
                            codegen_attribute_assignment(node_idx);
                            break;
                        }
                        if(lhs_kind == AstNodeKind::EXPRESSION_BINARY)
                        {
                            codegen_subscript_assignment(node_idx);
                            break;
                        }

                        // augmented assignment
                        if(kind.operator_kind != AstOperatorKind::NOP)
                        {
                            codegen_binary_expression(node_idx);
                        }
                        else
                        {
                            // just compute the RHS
                            codegen_node(children[1]);
                        }
                        emit_variable_store(source_offset, lhs_idx);
                        break;
                    }

                case AstNodeKind::STATEMENT_DEL:
                    for(int32_t target_idx: children)
                    {
                        AstNodeKind target_kind =
                            av.kinds[target_idx].node_kind;
                        if(target_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                        {
                            codegen_attribute_target_delete(source_offset,
                                                            target_idx);
                            continue;
                        }
                        if(target_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            emit_variable_delete(source_offset, target_idx);
                            continue;
                        }
                        throw std::runtime_error(
                            "We don't support del targets except variables and "
                            "attributes yet");
                    }
                    break;

                case AstNodeKind::EXPRESSION_BINARY:
                    if(kind.operator_kind == AstOperatorKind::SUBSCRIPT)
                    {
                        ScopedRegister receiver_reg =
                            codegen_node_to_register(children[0]);
                        codegen_node(children[1]);
                        code_obj->emit_load_subscript(source_offset,
                                                      receiver_reg.reg);
                        break;
                    }
                    codegen_binary_expression(node_idx);
                    break;

                case AstNodeKind::EXPRESSION_UNARY:
                    {
                        OpTableEntry entry =
                            get_operator_entry(kind.operator_kind);
                        codegen_node(children[0]);
                        code_obj->emit_unary_op(source_offset, entry.standard);
                        break;
                    }
                case AstNodeKind::EXPRESSION_LITERAL:
                    {

                        switch(kind.operator_kind)
                        {
                            case AstOperatorKind::NONE:
                                code_obj->emit_lda_none(source_offset);
                                break;
                            case AstOperatorKind::TRUE:
                                code_obj->emit_lda_true(source_offset);
                                break;
                            case AstOperatorKind::FALSE:
                                code_obj->emit_lda_false(source_offset);
                                break;

                            case AstOperatorKind::NUMBER:
                                {
                                    Value val =
                                        av.constants[node_idx].as_value();
                                    if(val.is_smi8())
                                    {
                                        code_obj->emit_lda_smi(source_offset,
                                                               val.get_smi());
                                    }
                                    else
                                    {
                                        uint32_t constant_idx =
                                            code_obj->allocate_constant(val);
                                        code_obj->emit_lda_constant(
                                            source_offset, constant_idx);
                                        break;
                                    }
                                    break;
                                }

                            case AstOperatorKind::STRING:
                                {
                                    uint32_t constant_idx =
                                        code_obj->allocate_constant(
                                            av.constants[node_idx]);
                                    code_obj->emit_lda_constant(source_offset,
                                                                constant_idx);
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
                            codegen_node_to_register(children[0]);
                        TemporaryReg prod_reg(this);
                        int32_t recv = recv_reg.reg;
                        int32_t prod = prod_reg;
                        for(size_t i = 1; i < children.size(); ++i)
                        {
                            bool last = i == children.size() - 1;
                            if(last)
                                prod = -1;

                            codegen_comparison_fragment(children[i], recv,
                                                        prod);

                            if(!last)
                            {
                                code_obj->emit_jump_if_false(source_offset,
                                                             skip_target);
                            }
                            std::swap(recv, prod);
                        }
                        skip_target.resolve();

                        break;
                    }

                case AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY:
                    {
                        JumpTarget skip_target(code_obj);
                        codegen_node(children[0]);
                        switch(kind.operator_kind)
                        {
                            case AstOperatorKind::SHORTCUTTING_AND:
                                code_obj->emit_jump_if_false(source_offset,
                                                             skip_target);
                                break;
                            case AstOperatorKind::SHORTCUTTING_OR:
                                code_obj->emit_jump_if_true(source_offset,
                                                            skip_target);
                                break;
                            default:
                                assert(0);
                                break;
                        }
                        codegen_node(children[1]);
                        skip_target.resolve();
                        break;
                    }

                case AstNodeKind::EXPRESSION_CALL:
                    codegen_function_call(node_idx);
                    break;

                case AstNodeKind::EXPRESSION_ATTRIBUTE:
                    {
                        ScopedRegister receiver_reg =
                            codegen_node_to_register(children[0]);
                        uint8_t constant_idx =
                            code_obj->allocate_constant(av.constants[node_idx]);
                        uint8_t cache_idx =
                            code_obj->allocate_attribute_read_cache();
                        code_obj->emit_load_attr(source_offset,
                                                 receiver_reg.reg, constant_idx,
                                                 cache_idx);
                        break;
                    }

                case AstNodeKind::STATEMENT_SEQUENCE:
                case AstNodeKind::STATEMENT_EXPRESSION:
                    for(int32_t ch_idx: children)
                    {
                        codegen_node(ch_idx);
                    }
                    break;

                case AstNodeKind::STATEMENT_IF:
                    {
                        JumpTarget done_target(code_obj);

                        for(size_t i = 0; i < children.size() - 1; i += 2)
                        {
                            JumpTarget next_target(code_obj);
                            codegen_node(
                                children[i + 0]);  // condition, initial check
                            code_obj->emit_jump_if_false(source_offset,
                                                         next_target);
                            codegen_node(children[i + 1]);  // then

                            if(i + 2 != children.size())
                            {
                                // if we have more to emit, we have to generate
                                // a jump to the done target. otherwise, we'll
                                // just fall through
                                code_obj->emit_jump(source_offset, done_target);
                            }
                            next_target.resolve();
                        }
                        if(children.size() & 1)  // odd -> else
                        {
                            codegen_node(children.back());  // else
                        }
                        done_target.resolve();

                        break;
                    }

                case AstNodeKind::STATEMENT_WHILE:
                    {
                        JumpTarget loop_start_target(code_obj);
                        JumpTarget else_target(code_obj);
                        JumpTarget break_target(code_obj);
                        JumpTarget continue_target(code_obj);
                        codegen_node(children[0]);  // condition, initial check
                        code_obj->emit_jump_if_false(source_offset,
                                                     else_target);

                        loop_start_target.resolve();

                        loop_targets.emplace_back(&break_target,
                                                  &continue_target);
                        codegen_node(children[1]);  // body
                        loop_targets.pop_back();

                        continue_target.resolve();
                        codegen_node(
                            children[0]);  // condition, non-initial check
                        code_obj->emit_jump_if_true(source_offset,
                                                    loop_start_target);
                        else_target.resolve();
                        if(children.size() == 3)
                        {
                            codegen_node(children[2]);  // else clause of a loop
                        }
                        break_target.resolve();
                        break;
                    }

                case AstNodeKind::STATEMENT_FOR:
                    {
                        int32_t target_idx = children[0];
                        std::optional<uint8_t> range_call_arity =
                            direct_range_call_arity(children[1]);
                        if(range_call_arity.has_value())
                        {
                            codegen_direct_range_for_loop(node_idx, target_idx,
                                                          *range_call_arity);
                            break;
                        }

                        int32_t iterable_idx = children[1];
                        int32_t body_idx = children[2];
                        int32_t else_idx =
                            children.size() == 4 ? children[3] : -1;
                        TemporaryReg iterator_reg(this);
                        JumpTarget else_target(code_obj);
                        JumpTarget break_target(code_obj);

                        codegen_node(iterable_idx);
                        code_obj->emit_get_iter(source_offset);
                        code_obj->emit_star(source_offset, iterator_reg);
                        codegen_iterator_driven_for_loop(
                            source_offset, target_idx, body_idx, iterator_reg,
                            else_target, break_target);
                        else_target.resolve();
                        if(else_idx >= 0)
                        {
                            codegen_node(else_idx);
                        }
                        break_target.resolve();
                        break;
                    }

                case AstNodeKind::STATEMENT_BREAK:
                    if(loop_targets.empty())
                    {
                        throw std::runtime_error(
                            "SyntaxError: 'break' outside loop");
                    }
                    else
                    {
                        code_obj->emit_jump(source_offset,
                                            *loop_targets.back().break_target);
                    }
                    break;

                case AstNodeKind::STATEMENT_CONTINUE:
                    if(loop_targets.empty())
                    {
                        throw std::runtime_error(
                            "SyntaxError: 'continue' not properly in loop");
                    }
                    else
                    {
                        code_obj->emit_jump(
                            source_offset,
                            *loop_targets.back().continue_target);
                    }
                    break;

                case AstNodeKind::STATEMENT_RETURN:
                    if(mode() != Mode::Function)
                    {
                        throw std::runtime_error(
                            "SyntaxError: 'return' outside function");
                    }
                    if(!children.empty())
                    {
                        codegen_node(children[0]);
                    }
                    else
                    {
                        code_obj->emit_lda_none(source_offset);
                    }
                    code_obj->emit_return(source_offset);
                    break;

                case AstNodeKind::STATEMENT_PASS:
                case AstNodeKind::STATEMENT_GLOBAL:
                case AstNodeKind::STATEMENT_NONLOCAL:
                    break;

                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                    codegen_function_definition(node_idx);
                    break;

                case AstNodeKind::STATEMENT_CLASS_DEF:
                    codegen_class_definition(node_idx);
                    break;

                case AstNodeKind::EXPRESSION_TUPLE:
                    codegen_tuple_literal(node_idx);
                    break;

                case AstNodeKind::EXPRESSION_LIST:
                    codegen_list_literal(node_idx);
                    break;

                case AstNodeKind::EXPRESSION_DICT:
                    codegen_dict_literal(node_idx);
                    break;

                case AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT:
                    throw std::runtime_error(
                        "should not end here - this is handled by "
                        "EXPRESSION_COMPARISON");

                case AstNodeKind::PARAMETER_SEQUENCE:
                case AstNodeKind::PARAMETER:
                case AstNodeKind::PARAMETER_VARARGS:
                    throw std::runtime_error("should not end here - this is "
                                             "handled by function definitions");
            }
        }
    };

    CodeObject *AstCodegen::run_module()
    {
        codegen_node(body_idx);
        code_obj->emit_halt(0);
        CodeObject *result = code_obj->finalize(max_temporary_reg);
        return incref(result);
    }

    uint32_t count_positional_parameters(const AstVector &av,
                                         AstChildren param_children)
    {
        uint32_t n_positional_parameters = 0;
        for(int32_t param_idx: param_children)
        {
            if(av.kinds[param_idx].node_kind == AstNodeKind::PARAMETER_VARARGS)
            {
                break;
            }
            ++n_positional_parameters;
        }
        return n_positional_parameters;
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

    CodeObject *codegen_function(const AstVector &av, Scope *module_scope,
                                 CodeObjectBuilder *parent_code_obj,
                                 int32_t node_idx)
    {
        AstChildren children = av.children[node_idx];
        uint32_t source_offset = av.source_offsets[node_idx];
        AstChildren param_children = av.children[children[0]];
        Scope *local_scope =
            make_internal_raw<Scope>(parent_code_obj->local_scope());
        CodeObjectBuilder fun_obj(av.compilation_unit, module_scope,
                                  local_scope, Value::None());

        fun_obj.set_name(av.constants[node_idx]);
        fun_obj.n_parameters() = param_children.size();
        fun_obj.n_positional_parameters() =
            count_positional_parameters(av, param_children);
        if(has_varargs_parameter(av, param_children))
        {
            fun_obj.parameter_flags() |= FunctionParameterFlags::HasVarArgs;
        }
        for(int32_t ch: param_children)
        {
            assert(av.kinds[ch].node_kind == AstNodeKind::PARAMETER ||
                   av.kinds[ch].node_kind == AstNodeKind::PARAMETER_VARARGS);
            fun_obj.get_local_scope_ptr()->register_slot_index_for_write(
                TValue<String>(av.constants[ch]));
        }
        reserve_parameter_padding_and_frame_header(&fun_obj);

        AstCodegen fun_builder{av,          module_scope,
                               &fun_obj,    AstCodegen::Mode::Function,
                               children[1], param_children};
        return fun_builder.run_function_body(source_offset, children[1]);
    }

    CodeObject *codegen_class(const AstVector &av, Scope *module_scope,
                              CodeObjectBuilder *parent_code_obj,
                              int32_t node_idx)
    {
        AstChildren children = av.children[node_idx];
        uint32_t source_offset = av.source_offsets[node_idx];
        int32_t body_idx = children[1];
        Scope *local_scope =
            make_internal_raw<Scope>(parent_code_obj->local_scope());
        CodeObjectBuilder class_obj(av.compilation_unit, module_scope,
                                    local_scope, parent_code_obj->name());

        class_obj.n_parameters() = 2;
        class_obj.get_local_scope_ptr()->reserve_empty_slots(2);
        reserve_parameter_padding_and_frame_header(&class_obj);

        AstCodegen class_builder{av,         module_scope,
                                 &class_obj, AstCodegen::Mode::Class,
                                 body_idx,   {}};
        return class_builder.run_class_body(source_offset, body_idx);
    }

    CodeObject *AstCodegen::run_function_body(uint32_t source_offset,
                                              int32_t body_idx)
    {
        emit_local_binding_prologue();
        codegen_node(body_idx);
        // finally, emit return None just in case. as a future optimisation, we
        // could check that all return paths already have a return statement
        code_obj->emit_lda_none(source_offset);
        code_obj->emit_return(source_offset);
        return code_obj->finalize(max_temporary_reg);
    }

    CodeObject *AstCodegen::run_class_body(uint32_t source_offset,
                                           int32_t body_idx)
    {
        emit_local_binding_prologue();
        codegen_node(body_idx);
        code_obj->emit_build_class(source_offset);
        return code_obj->finalize(max_temporary_reg);
    }

    CodeObject *codegen_module(const AstVector &av, TValue<String> module_name)
    {
        Scope *module_scope = make_internal_raw<Scope>(
            active_vm()->get_builtin_scope().extract());
        CodeObjectBuilder module_obj(av.compilation_unit, module_scope, nullptr,
                                     module_name);
        AstCodegen builder{av,           module_scope,
                           &module_obj,  AstCodegen::Mode::Module,
                           av.root_node, {}};
        return builder.run_module();
    }

}  // namespace cl
