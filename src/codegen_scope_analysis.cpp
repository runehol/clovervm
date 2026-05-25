#include "codegen_scope_analysis.h"

#include "code_object_builder.h"
#include "scope.h"
#include "str.h"
#include "typed_value.h"
#include <cassert>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace cl
{
    namespace
    {
        TValue<String> ast_string_constant(Value value)
        {
            return TValue<String>::from_value_assumed(value);
        }

        struct StringNameHash
        {
            size_t operator()(TValue<String> name) const
            {
                return size_t(string_hash(name));
            }
        };

        struct StringNameEq
        {
            bool operator()(TValue<String> left, TValue<String> right) const
            {
                return string_eq(left, right);
            }
        };

        struct FlowState
        {
            std::vector<Presence> local_presence;
            std::vector<bool> may_be_entry_value;
        };

        struct AnalysisState
        {
            explicit AnalysisState(CodegenMode mode, size_t n_nodes)
                : result(mode, n_nodes)
            {
            }

            ScopeAnalysis result;
            std::unordered_map<TValue<String>, int32_t, StringNameHash,
                               StringNameEq>
                binding_indices;
        };

        struct GlobalDeclarationState
        {
            struct NameState
            {
                bool parameter = false;
                bool used = false;
                bool assigned = false;
                bool annotated = false;
                bool global = false;
            };

            CodegenMode mode;
            std::unordered_map<TValue<String>, NameState, StringNameHash,
                               StringNameEq>
                names;
        };

        class ScopeAnalyzer
        {
        public:
            ScopeAnalyzer(const AstVector &_av, CodeObjectBuilder *_code_obj)
                : av(_av), code_obj(_code_obj)
            {
            }

            ScopeAnalysis run(int32_t body_idx, CodegenMode mode,
                              AstChildren param_children)
            {
                AnalysisState analysis(mode, av.size());
                validate_global_declarations(mode, body_idx, param_children);
                collect_global_declarations(analysis, body_idx);
                if(mode == CodegenMode::Function)
                {
                    for(int32_t param_idx: param_children)
                    {
                        ensure_local_binding(analysis, av.constants[param_idx],
                                             Presence::Present);
                    }
                }
                collect_code_object_bindings(analysis, body_idx);
                FlowState state = initial_flow_state(analysis.result);
                analyze_flow_node(analysis, body_idx, state);
                return std::move(analysis.result);
            }

        private:
            const AstVector &av;
            CodeObjectBuilder *code_obj;

            static bool handler_has_type(AstChildren handler_children)
            {
                return handler_children.size() >= 2;
            }

            static bool handler_has_name(AstChildren handler_children)
            {
                return handler_children.size() == 3;
            }

            static int32_t handler_type_idx(AstChildren handler_children)
            {
                assert(handler_has_type(handler_children));
                return handler_children[0];
            }

            static int32_t handler_name_idx(AstChildren handler_children)
            {
                assert(handler_has_name(handler_children));
                return handler_children[1];
            }

            static int32_t handler_body_idx(AstChildren handler_children)
            {
                return handler_children.back();
            }

            static Presence merge_presence(Presence left, Presence right)
            {
                if(left == right)
                {
                    return left;
                }
                return Presence::Maybe;
            }

            int32_t find_binding_idx(const AnalysisState &analysis,
                                     Value name) const
            {
                auto binding_iter =
                    analysis.binding_indices.find(ast_string_constant(name));
                if(binding_iter == analysis.binding_indices.end())
                {
                    return -1;
                }
                return binding_iter->second;
            }

            BindingInfo *find_binding(AnalysisState &analysis, Value name)
            {
                int32_t idx = find_binding_idx(analysis, name);
                if(idx < 0)
                {
                    return nullptr;
                }
                return &analysis.result.bindings[idx];
            }

            const BindingInfo *find_binding(const AnalysisState &analysis,
                                            Value name) const
            {
                int32_t idx = find_binding_idx(analysis, name);
                if(idx < 0)
                {
                    return nullptr;
                }
                return &analysis.result.bindings[idx];
            }

            BindingInfo &
            ensure_local_binding(AnalysisState &analysis, Value name,
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

                uint32_t slot_idx = code_obj->get_local_scope_ptr()
                                        ->register_slot_index_for_write(
                                            ast_string_constant(name));
                int32_t binding_idx = int32_t(analysis.result.bindings.size());
                analysis.result.bindings.push_back(
                    BindingInfo{name, BindingScope::Local, slot_idx,
                                initial_presence, false});
                analysis.binding_indices[ast_string_constant(name)] =
                    binding_idx;
                return analysis.result.bindings.back();
            }

            BindingInfo *
            ensure_binding(AnalysisState &analysis, Value name,
                           Presence initial_presence = Presence::Missing)
            {
                if(analysis.result.mode == CodegenMode::Module)
                {
                    return nullptr;
                }
                return &ensure_local_binding(analysis, name, initial_presence);
            }

            BindingInfo &ensure_global_binding(AnalysisState &analysis,
                                               Value name)
            {
                if(BindingInfo *binding = find_binding(analysis, name))
                {
                    binding->scope = BindingScope::Global;
                    binding->initial_presence = Presence::Maybe;
                    binding->needs_entry_clear = false;
                    return *binding;
                }

                int32_t binding_idx = int32_t(analysis.result.bindings.size());
                analysis.result.bindings.push_back(BindingInfo{
                    name, BindingScope::Global, 0, Presence::Maybe, false});
                analysis.binding_indices[ast_string_constant(name)] =
                    binding_idx;
                return analysis.result.bindings.back();
            }

            BindingInfo binding_for_name(const AnalysisState &analysis,
                                         Value name) const
            {
                if(const BindingInfo *binding = find_binding(analysis, name))
                {
                    return *binding;
                }
                return BindingInfo{name, BindingScope::Global, 0,
                                   Presence::Maybe, false};
            }

            NameAccessAnalysis make_access(AnalysisState &analysis, Value name,
                                           const FlowState &state,
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
                        analysis.result.bindings[size_t(binding_idx)]
                            .needs_entry_clear = true;
                    }
                    return NameAccessAnalysis{
                        BindingScope::Local,
                        state.local_presence[size_t(binding_idx)],
                        binding.local_slot_idx};
                }

                return NameAccessAnalysis{BindingScope::Global, Presence::Maybe,
                                          0};
            }

            void mark_local_presence(AnalysisState &analysis, FlowState &state,
                                     Value name, Presence presence)
            {
                int32_t binding_idx = find_binding_idx(analysis, name);
                if(binding_idx < 0 ||
                   analysis.result.bindings[size_t(binding_idx)].scope !=
                       BindingScope::Local)
                {
                    return;
                }
                state.local_presence[size_t(binding_idx)] = presence;
                state.may_be_entry_value[size_t(binding_idx)] = false;
            }

            GlobalDeclarationState::NameState &
            global_name_state(GlobalDeclarationState &state, Value name)
            {
                return state.names[ast_string_constant(name)];
            }

            void mark_global_validation_use(GlobalDeclarationState &state,
                                            Value name)
            {
                global_name_state(state, name).used = true;
            }

            void
            mark_global_validation_assignment(GlobalDeclarationState &state,
                                              Value name)
            {
                global_name_state(state, name).assigned = true;
            }

            void
            mark_global_validation_annotation(GlobalDeclarationState &state,
                                              Value name)
            {
                GlobalDeclarationState::NameState &entry =
                    global_name_state(state, name);
                if(entry.global && state.mode == CodegenMode::Function)
                {
                    throw std::runtime_error(
                        "SyntaxError: annotated name can't be global");
                }
                entry.annotated = true;
            }

            void declare_global_name(GlobalDeclarationState &state, Value name)
            {
                GlobalDeclarationState::NameState &entry =
                    global_name_state(state, name);
                if(entry.parameter)
                {
                    throw std::runtime_error(
                        "SyntaxError: name is parameter and global");
                }
                if(entry.annotated)
                {
                    throw std::runtime_error(
                        "SyntaxError: annotated name can't be global");
                }
                if(entry.assigned)
                {
                    throw std::runtime_error(
                        "SyntaxError: name is assigned to before global "
                        "declaration");
                }
                if(entry.used)
                {
                    throw std::runtime_error("SyntaxError: name is used prior "
                                             "to global declaration");
                }
                entry.global = true;
            }

            void validate_global_declaration_expression(
                GlobalDeclarationState &state, int32_t node_idx)
            {
                AstKind kind = av.kinds[node_idx];
                const AstChildren &children = av.children[node_idx];

                switch(kind.node_kind)
                {
                    case AstNodeKind::EXPRESSION_VARIABLE_REFERENCE:
                        mark_global_validation_use(state,
                                                   av.constants[node_idx]);
                        return;

                    case AstNodeKind::EXPRESSION_ATTRIBUTE:
                        validate_global_declaration_expression(state,
                                                               children[0]);
                        return;

                    default:
                        for(int32_t child_idx: children)
                        {
                            validate_global_declaration_expression(state,
                                                                   child_idx);
                        }
                        return;
                }
            }

            void validate_global_declaration_assignment_target(
                GlobalDeclarationState &state, int32_t target_idx,
                bool augmented_assignment)
            {
                AstKind target_kind = av.kinds[target_idx];
                AstChildren target_children = av.children[target_idx];

                if(target_kind.node_kind ==
                   AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                {
                    if(augmented_assignment)
                    {
                        mark_global_validation_use(state,
                                                   av.constants[target_idx]);
                    }
                    mark_global_validation_assignment(state,
                                                      av.constants[target_idx]);
                    return;
                }

                if(target_kind.node_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                {
                    validate_global_declaration_expression(state,
                                                           target_children[0]);
                    return;
                }

                if(target_kind.node_kind == AstNodeKind::EXPRESSION_BINARY &&
                   target_kind.operator_kind == AstOperatorKind::SUBSCRIPT)
                {
                    validate_global_declaration_expression(state,
                                                           target_children[0]);
                    validate_global_declaration_expression(state,
                                                           target_children[1]);
                    return;
                }

                validate_global_declaration_expression(state, target_idx);
            }

            void
            validate_global_declarations_in_node(GlobalDeclarationState &state,
                                                 int32_t node_idx)
            {
                AstKind kind = av.kinds[node_idx];
                const AstChildren &children = av.children[node_idx];

                switch(kind.node_kind)
                {
                    case AstNodeKind::STATEMENT_GLOBAL:
                        for(int32_t name_idx: children)
                        {
                            declare_global_name(state, av.constants[name_idx]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_FUNCTION_DEF:
                        {
                            AstChildren param_children =
                                av.children[children[0]];
                            for(int32_t param_idx: param_children)
                            {
                                for(int32_t default_idx: av.children[param_idx])
                                {
                                    validate_global_declaration_expression(
                                        state, default_idx);
                                }
                            }
                            mark_global_validation_assignment(
                                state, av.constants[node_idx]);
                            return;
                        }

                    case AstNodeKind::STATEMENT_CLASS_DEF:
                        for(int32_t base_idx: av.children[children[0]])
                        {
                            validate_global_declaration_expression(state,
                                                                   base_idx);
                        }
                        mark_global_validation_assignment(
                            state, av.constants[node_idx]);
                        return;

                    case AstNodeKind::STATEMENT_IMPORT:
                        for(int32_t alias_idx: children)
                        {
                            mark_global_validation_assignment(
                                state, av.constants[av.children[alias_idx][0]]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_IMPORT_FROM:
                        for(size_t child_offset = 1;
                            child_offset < children.size(); ++child_offset)
                        {
                            int32_t alias_idx = children[child_offset];
                            if(av.kinds[alias_idx].node_kind ==
                               AstNodeKind::IMPORT_STAR)
                            {
                                continue;
                            }
                            mark_global_validation_assignment(
                                state, av.constants[av.children[alias_idx][0]]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_ASSIGN:
                    case AstNodeKind::EXPRESSION_ASSIGN:
                        {
                            int32_t lhs_idx = children[0];
                            if(kind.operator_kind != AstOperatorKind::NOP)
                            {
                                validate_global_declaration_assignment_target(
                                    state, lhs_idx, true);
                            }
                            validate_global_declaration_expression(state,
                                                                   children[1]);
                            if(kind.operator_kind == AstOperatorKind::NOP)
                            {
                                validate_global_declaration_assignment_target(
                                    state, lhs_idx, false);
                            }
                            return;
                        }

                    case AstNodeKind::STATEMENT_ANN_ASSIGN:
                        if(av.kinds[children[0]].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            mark_global_validation_annotation(
                                state, av.constants[children[0]]);
                        }
                        else
                        {
                            validate_global_declaration_assignment_target(
                                state, children[0], false);
                        }
                        if(children.size() == 3)
                        {
                            validate_global_declaration_expression(state,
                                                                   children[2]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_DEL:
                        for(int32_t target_idx: children)
                        {
                            validate_global_declaration_assignment_target(
                                state, target_idx, false);
                        }
                        return;

                    case AstNodeKind::STATEMENT_FOR:
                        validate_global_declaration_expression(state,
                                                               children[1]);
                        validate_global_declaration_assignment_target(
                            state, children[0], false);
                        validate_global_declarations_in_node(state,
                                                             children[2]);
                        if(children.size() == 4)
                        {
                            validate_global_declarations_in_node(state,
                                                                 children[3]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_WITH:
                        for(size_t child_offset = 0;
                            child_offset + 1 < children.size(); ++child_offset)
                        {
                            validate_global_declarations_in_node(
                                state, children[child_offset]);
                        }
                        validate_global_declarations_in_node(state,
                                                             children.back());
                        return;

                    case AstNodeKind::WITH_ITEM:
                        validate_global_declaration_expression(state,
                                                               children[0]);
                        if(children.size() == 2)
                        {
                            validate_global_declaration_assignment_target(
                                state, children[1], false);
                        }
                        return;

                    case AstNodeKind::STATEMENT_EXCEPT_HANDLER:
                        if(handler_has_type(children))
                        {
                            validate_global_declaration_expression(
                                state, handler_type_idx(children));
                        }
                        if(handler_has_name(children))
                        {
                            validate_global_declaration_assignment_target(
                                state, handler_name_idx(children), false);
                        }
                        validate_global_declarations_in_node(
                            state, handler_body_idx(children));
                        return;

                    case AstNodeKind::STATEMENT_SEQUENCE:
                        for(int32_t child_idx: children)
                        {
                            validate_global_declarations_in_node(state,
                                                                 child_idx);
                        }
                        return;

                    default:
                        if(is_expression(kind.node_kind))
                        {
                            validate_global_declaration_expression(state,
                                                                   node_idx);
                            return;
                        }
                        for(int32_t child_idx: children)
                        {
                            validate_global_declarations_in_node(state,
                                                                 child_idx);
                        }
                        return;
                }
            }

            void validate_global_declarations(CodegenMode mode,
                                              int32_t body_idx,
                                              AstChildren param_children)
            {
                GlobalDeclarationState state{mode, {}};
                if(mode == CodegenMode::Function)
                {
                    for(int32_t param_idx: param_children)
                    {
                        global_name_state(state, av.constants[param_idx])
                            .parameter = true;
                    }
                }

                validate_global_declarations_in_node(state, body_idx);
            }

            void collect_global_declarations(AnalysisState &analysis,
                                             int32_t node_idx)
            {
                AstKind kind = av.kinds[node_idx];
                AstChildren children = av.children[node_idx];

                switch(kind.node_kind)
                {
                    case AstNodeKind::STATEMENT_GLOBAL:
                        for(int32_t name_idx: children)
                        {
                            ensure_global_binding(analysis,
                                                  av.constants[name_idx]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_FUNCTION_DEF:
                    case AstNodeKind::STATEMENT_CLASS_DEF:
                        return;

                    default:
                        break;
                }

                for(int32_t child_idx: children)
                {
                    collect_global_declarations(analysis, child_idx);
                }
            }

            void collect_code_object_bindings(AnalysisState &analysis,
                                              int32_t node_idx)
            {
                AstKind kind = av.kinds[node_idx];
                AstChildren children = av.children[node_idx];

                switch(kind.node_kind)
                {
                    case AstNodeKind::STATEMENT_FUNCTION_DEF:
                    case AstNodeKind::STATEMENT_CLASS_DEF:
                        ensure_binding(analysis, av.constants[node_idx]);
                        return;

                    case AstNodeKind::STATEMENT_IMPORT:
                        for(int32_t alias_idx: children)
                        {
                            ensure_binding(
                                analysis,
                                av.constants[av.children[alias_idx][0]]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_IMPORT_FROM:
                        for(size_t child_offset = 1;
                            child_offset < children.size(); ++child_offset)
                        {
                            int32_t alias_idx = children[child_offset];
                            if(av.kinds[alias_idx].node_kind ==
                               AstNodeKind::IMPORT_STAR)
                            {
                                continue;
                            }
                            ensure_binding(
                                analysis,
                                av.constants[av.children[alias_idx][0]]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_ASSIGN:
                    case AstNodeKind::EXPRESSION_ASSIGN:
                        {
                            int32_t lhs_idx = children[0];
                            if(av.kinds[lhs_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                ensure_binding(analysis, av.constants[lhs_idx]);
                            }
                            break;
                        }

                    case AstNodeKind::STATEMENT_ANN_ASSIGN:
                        if(children.size() == 3 &&
                           av.kinds[children[0]].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            ensure_binding(analysis, av.constants[children[0]]);
                        }
                        if(children.size() == 3)
                        {
                            collect_code_object_bindings(analysis, children[2]);
                        }
                        if(!ann_assign_is_simple(node_idx))
                        {
                            collect_code_object_bindings(analysis, children[0]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_DEL:
                        for(int32_t target_idx: children)
                        {
                            if(av.kinds[target_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                ensure_binding(analysis,
                                               av.constants[target_idx]);
                            }
                        }
                        break;

                    case AstNodeKind::STATEMENT_FOR:
                        {
                            int32_t target_idx = children[0];
                            if(av.kinds[target_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                ensure_binding(analysis,
                                               av.constants[target_idx]);
                            }
                        }
                        break;

                    case AstNodeKind::WITH_ITEM:
                        if(children.size() == 2)
                        {
                            int32_t target_idx = children[1];
                            if(av.kinds[target_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                ensure_binding(analysis,
                                               av.constants[target_idx]);
                            }
                        }
                        break;

                    case AstNodeKind::STATEMENT_EXCEPT_HANDLER:
                        if(handler_has_name(children))
                        {
                            int32_t name_idx = handler_name_idx(children);
                            ensure_binding(analysis, av.constants[name_idx]);
                        }
                        if(handler_has_type(children))
                        {
                            collect_code_object_bindings(
                                analysis, handler_type_idx(children));
                        }
                        collect_code_object_bindings(
                            analysis, handler_body_idx(children));
                        return;

                    default:
                        break;
                }

                for(int32_t child_idx: children)
                {
                    collect_code_object_bindings(analysis, child_idx);
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
                    state.may_be_entry_value.push_back(
                        binding.initial_presence != Presence::Present);
                }
                return state;
            }

            bool ann_assign_is_simple(int32_t node_idx) const
            {
                assert(av.kinds[node_idx].node_kind ==
                       AstNodeKind::STATEMENT_ANN_ASSIGN);
                return av.constants[node_idx] == Value::True();
            }

            FlowState merge_flow_states(const FlowState &left,
                                        const FlowState &right) const
            {
                assert(left.local_presence.size() ==
                       right.local_presence.size());
                assert(left.may_be_entry_value.size() ==
                       right.may_be_entry_value.size());
                FlowState merged;
                merged.local_presence.reserve(left.local_presence.size());
                merged.may_be_entry_value.reserve(
                    left.may_be_entry_value.size());
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

            bool flow_states_equal(const FlowState &left,
                                   const FlowState &right) const
            {
                assert(left.local_presence.size() ==
                       right.local_presence.size());
                assert(left.may_be_entry_value.size() ==
                       right.may_be_entry_value.size());
                return left.local_presence == right.local_presence &&
                       left.may_be_entry_value == right.may_be_entry_value;
            }

            void annotate_for_target_store(AnalysisState &analysis,
                                           int32_t target_idx, FlowState &state)
            {
                if(av.kinds[target_idx].node_kind ==
                   AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                {
                    analysis.result.stores[target_idx] = make_access(
                        analysis, av.constants[target_idx], state, false);
                    mark_local_presence(analysis, state,
                                        av.constants[target_idx],
                                        Presence::Present);
                }
                else
                {
                    analyze_flow_node(analysis, target_idx, state);
                }
            }

            FlowState analyze_while_condition_entry_fixed_point(
                AnalysisState &analysis, const FlowState &initial_entry,
                int32_t condition_idx, int32_t body_idx)
            {
                FlowState condition_entry = initial_entry;
                size_t max_iterations = analysis.result.bindings.size() * 2 + 3;
                for(size_t iteration = 0; iteration < max_iterations;
                    ++iteration)
                {
                    FlowState body_exit = condition_entry;
                    analyze_flow_node(analysis, condition_idx, body_exit);
                    analyze_flow_node(analysis, body_idx, body_exit);
                    FlowState merged_entry =
                        merge_flow_states(initial_entry, body_exit);
                    if(flow_states_equal(merged_entry, condition_entry))
                    {
                        return condition_entry;
                    }
                    condition_entry = merged_entry;
                }
                assert(false && "loop flow analysis did not converge");
                return condition_entry;
            }

            FlowState analyze_for_body_entry_fixed_point(
                AnalysisState &analysis, const FlowState &before_loop_entry,
                int32_t target_idx, int32_t body_idx)
            {
                FlowState first_iteration_entry = before_loop_entry;
                annotate_for_target_store(analysis, target_idx,
                                          first_iteration_entry);
                FlowState body_entry = first_iteration_entry;
                size_t max_iterations = analysis.result.bindings.size() * 2 + 3;
                for(size_t iteration = 0; iteration < max_iterations;
                    ++iteration)
                {
                    FlowState next_iteration_entry = body_entry;
                    analyze_flow_node(analysis, body_idx, next_iteration_entry);
                    annotate_for_target_store(analysis, target_idx,
                                              next_iteration_entry);
                    FlowState merged_entry = merge_flow_states(
                        first_iteration_entry, next_iteration_entry);
                    if(flow_states_equal(merged_entry, body_entry))
                    {
                        return body_entry;
                    }
                    body_entry = merged_entry;
                }
                assert(false && "for loop flow analysis did not converge");
                return body_entry;
            }

            void analyze_flow_node(AnalysisState &analysis, int32_t node_idx,
                                   FlowState &state)
            {
                AstKind kind = av.kinds[node_idx];
                AstChildren children = av.children[node_idx];

                auto annotate_read = [&](int32_t read_idx) {
                    analysis.result.loads[read_idx] = make_access(
                        analysis, av.constants[read_idx], state, true);
                };
                auto annotate_write = [&](int32_t write_idx) {
                    analysis.result.stores[write_idx] = make_access(
                        analysis, av.constants[write_idx], state, false);
                    mark_local_presence(analysis, state,
                                        av.constants[write_idx],
                                        Presence::Present);
                };
                auto annotate_delete = [&](int32_t delete_idx) {
                    analysis.result.deletes[delete_idx] = make_access(
                        analysis, av.constants[delete_idx], state, true);
                    mark_local_presence(analysis, state,
                                        av.constants[delete_idx],
                                        Presence::Missing);
                };
                auto annotate_named_definition = [&](int32_t definition_idx) {
                    analysis.result.stores[definition_idx] = make_access(
                        analysis, av.constants[definition_idx], state, false);
                    mark_local_presence(analysis, state,
                                        av.constants[definition_idx],
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
                                analyze_flow_node(analysis, children[1], state);
                                annotate_write(lhs_idx);
                                break;
                            }

                            analyze_flow_node(analysis, lhs_idx, state);
                            analyze_flow_node(analysis, children[1], state);
                            break;
                        }

                    case AstNodeKind::STATEMENT_IMPORT:
                        for(int32_t alias_idx: children)
                        {
                            annotate_write(av.children[alias_idx][0]);
                        }
                        break;

                    case AstNodeKind::STATEMENT_IMPORT_FROM:
                        for(size_t child_offset = 1;
                            child_offset < children.size(); ++child_offset)
                        {
                            int32_t alias_idx = children[child_offset];
                            if(av.kinds[alias_idx].node_kind ==
                               AstNodeKind::IMPORT_STAR)
                            {
                                continue;
                            }
                            annotate_write(av.children[alias_idx][0]);
                        }
                        break;

                    case AstNodeKind::STATEMENT_ANN_ASSIGN:
                        if(children.size() == 3)
                        {
                            if(av.kinds[children[0]].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                analyze_flow_node(analysis, children[2], state);
                                annotate_write(children[0]);
                                break;
                            }
                            analyze_flow_node(analysis, children[0], state);
                            analyze_flow_node(analysis, children[2], state);
                        }
                        else if(!ann_assign_is_simple(node_idx))
                        {
                            analyze_flow_node(analysis, children[0], state);
                        }
                        break;

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
                                analyze_flow_node(analysis, target_idx, state);
                            }
                        }
                        break;

                    case AstNodeKind::STATEMENT_SEQUENCE:
                    case AstNodeKind::STATEMENT_EXPRESSION:
                        for(int32_t child_idx: children)
                        {
                            analyze_flow_node(analysis, child_idx, state);
                        }
                        break;

                    case AstNodeKind::STATEMENT_IF:
                        {
                            FlowState fallthrough = state;
                            std::optional<FlowState> merged_branches;
                            for(size_t idx = 0; idx + 1 < children.size();
                                idx += 2)
                            {
                                analyze_flow_node(analysis, children[idx],
                                                  fallthrough);
                                FlowState branch_state = fallthrough;
                                analyze_flow_node(analysis, children[idx + 1],
                                                  branch_state);
                                merged_branches =
                                    merged_branches.has_value()
                                        ? merge_flow_states(*merged_branches,
                                                            branch_state)
                                        : branch_state;
                            }
                            if(children.size() & 1)
                            {
                                analyze_flow_node(analysis, children.back(),
                                                  fallthrough);
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
                            FlowState condition_entry =
                                analyze_while_condition_entry_fixed_point(
                                    analysis, state, children[0], children[1]);
                            state = condition_entry;
                            analyze_flow_node(analysis, children[0], state);
                            if(children.size() == 3)
                            {
                                analyze_flow_node(analysis, children[2], state);
                            }
                            break;
                        }

                    case AstNodeKind::STATEMENT_FOR:
                        {
                            int32_t target_idx = children[0];
                            int32_t iterable_idx = children[1];
                            int32_t body_idx = children[2];
                            analyze_flow_node(analysis, iterable_idx, state);

                            FlowState body_state =
                                analyze_for_body_entry_fixed_point(
                                    analysis, state, target_idx, body_idx);
                            analyze_flow_node(analysis, body_idx, body_state);
                            state = merge_flow_states(state, body_state);

                            if(children.size() == 4)
                            {
                                analyze_flow_node(analysis, children[3], state);
                            }
                            break;
                        }

                    case AstNodeKind::STATEMENT_WITH:
                        {
                            FlowState with_state = state;
                            for(size_t child_offset = 0;
                                child_offset + 1 < children.size();
                                ++child_offset)
                            {
                                analyze_flow_node(analysis,
                                                  children[child_offset],
                                                  with_state);
                            }
                            analyze_flow_node(analysis, children.back(),
                                              with_state);
                            state = with_state;
                            break;
                        }

                    case AstNodeKind::WITH_ITEM:
                        analyze_flow_node(analysis, children[0], state);
                        if(children.size() == 2)
                        {
                            int32_t target_idx = children[1];
                            if(av.kinds[target_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                annotate_write(target_idx);
                            }
                            else
                            {
                                analyze_flow_node(analysis, target_idx, state);
                            }
                        }
                        break;

                    case AstNodeKind::STATEMENT_TRY:
                        {
                            FlowState normal_state = state;
                            analyze_flow_node(analysis, children[0],
                                              normal_state);

                            FlowState merged_state = normal_state;
                            for(size_t child_offset = 1;
                                child_offset < children.size(); ++child_offset)
                            {
                                int32_t handler_idx = children[child_offset];
                                AstChildren handler_children =
                                    av.children[handler_idx];
                                FlowState handler_state = state;
                                if(handler_has_type(handler_children))
                                {
                                    analyze_flow_node(
                                        analysis,
                                        handler_type_idx(handler_children),
                                        handler_state);
                                }
                                if(handler_has_name(handler_children))
                                {
                                    annotate_write(
                                        handler_name_idx(handler_children));
                                }
                                analyze_flow_node(
                                    analysis,
                                    handler_body_idx(handler_children),
                                    handler_state);
                                merged_state = merge_flow_states(merged_state,
                                                                 handler_state);
                            }

                            state = merged_state;
                            break;
                        }

                    case AstNodeKind::STATEMENT_FUNCTION_DEF:
                        {
                            AstChildren param_children =
                                av.children[children[0]];
                            for(int32_t param_idx: param_children)
                            {
                                for(int32_t default_idx: av.children[param_idx])
                                {
                                    analyze_flow_node(analysis, default_idx,
                                                      state);
                                }
                            }
                            annotate_named_definition(node_idx);
                            break;
                        }

                    case AstNodeKind::STATEMENT_CLASS_DEF:
                        for(int32_t base_idx: av.children[children[0]])
                        {
                            analyze_flow_node(analysis, base_idx, state);
                        }
                        annotate_named_definition(node_idx);
                        break;

                    case AstNodeKind::STATEMENT_RETURN:
                        if(!children.empty())
                        {
                            analyze_flow_node(analysis, children[0], state);
                        }
                        break;

                    case AstNodeKind::STATEMENT_RAISE:
                        if(!children.empty())
                        {
                            analyze_flow_node(analysis, children[0], state);
                        }
                        break;

                    case AstNodeKind::STATEMENT_ASSERT:
                        analyze_flow_node(analysis, children[0], state);
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
                            analyze_flow_node(analysis, child_idx, state);
                        }
                        break;
                }
            }
        };
    }  // namespace

    ScopeAnalysis analyze_code_object_scope(const AstVector &av,
                                            CodeObjectBuilder *target_code_obj,
                                            int32_t body_idx, CodegenMode mode,
                                            AstChildren param_children)
    {
        ScopeAnalyzer analyzer(av, target_code_obj);
        return analyzer.run(body_idx, mode, param_children);
    }

}  // namespace cl
