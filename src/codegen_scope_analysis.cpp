#include "codegen_scope_analysis.h"

#include "code_object_builder.h"
#include "scope.h"
#include "str.h"
#include "typed_value.h"
#include <cassert>
#include <optional>
#include <unordered_map>
#include <utility>

namespace cl
{
    namespace
    {
        TValue<String> ast_string_constant(Value value)
        {
            return TValue<String>::from_value_assumed(value);
        }

        AstChildren signature_parameter_nodes(const AstVector &av,
                                              int32_t signature_idx)
        {
            if(av.kinds[signature_idx].node_kind !=
               AstNodeKind::PARAMETER_SIGNATURE)
            {
                return av.children[signature_idx];
            }

            AstChildren result;
            for(int32_t group_idx: av.children[signature_idx])
            {
                for(int32_t param_idx: av.children[group_idx])
                {
                    result.push_back(param_idx);
                }
            }
            return result;
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

        struct FlowSummary
        {
            std::optional<FlowState> fallthrough;
            std::vector<FlowState> breaks;
            std::vector<FlowState> continues;
        };

        struct LocalTouches
        {
            std::vector<bool> assigned;
            std::vector<bool> deleted;
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

            Expected<ScopeAnalysis> run(int32_t body_idx, CodegenMode mode,
                                        AstChildren param_children)
            {
                AnalysisState analysis(mode, av.size());
                CL_TRY(validate_global_declarations(mode, body_idx,
                                                    param_children));
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
                return Expected<ScopeAnalysis>::ok(std::move(analysis.result));
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

            Expected<void>
            mark_global_validation_annotation(GlobalDeclarationState &state,
                                              Value name)
            {
                GlobalDeclarationState::NameState &entry =
                    global_name_state(state, name);
                if(entry.global && state.mode == CodegenMode::Function)
                {
                    return Expected<void>::raise_exception(
                        L"SyntaxError", L"annotated name can't be global");
                }
                entry.annotated = true;
                return Expected<void>::ok();
            }

            Expected<void> declare_global_name(GlobalDeclarationState &state,
                                               Value name)
            {
                GlobalDeclarationState::NameState &entry =
                    global_name_state(state, name);
                if(entry.parameter)
                {
                    return Expected<void>::raise_exception(
                        L"SyntaxError", L"name is parameter and global");
                }
                if(entry.annotated)
                {
                    return Expected<void>::raise_exception(
                        L"SyntaxError", L"annotated name can't be global");
                }
                if(entry.assigned)
                {
                    return Expected<void>::raise_exception(
                        L"SyntaxError",
                        L"name is assigned to before global declaration");
                }
                if(entry.used)
                {
                    return Expected<void>::raise_exception(
                        L"SyntaxError",
                        L"name is used prior to global declaration");
                }
                entry.global = true;
                return Expected<void>::ok();
            }

            Expected<void> validate_global_declaration_expression(
                GlobalDeclarationState &state, int32_t node_idx)
            {
                AstKind kind = av.kinds[node_idx];
                const AstChildren &children = av.children[node_idx];

                switch(kind.node_kind)
                {
                    case AstNodeKind::EXPRESSION_VARIABLE_REFERENCE:
                        mark_global_validation_use(state,
                                                   av.constants[node_idx]);
                        return Expected<void>::ok();

                    case AstNodeKind::EXPRESSION_ATTRIBUTE:
                        return validate_global_declaration_expression(
                            state, children[0]);

                    default:
                        for(int32_t child_idx: children)
                        {
                            CL_TRY(validate_global_declaration_expression(
                                state, child_idx));
                        }
                        return Expected<void>::ok();
                }
            }

            Expected<void> validate_global_declaration_assignment_target(
                GlobalDeclarationState &state, int32_t target_idx,
                bool augmented_assignment)
            {
                AstKind target_kind = av.kinds[target_idx];
                const AstChildren &target_children = av.children[target_idx];

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
                    return Expected<void>::ok();
                }

                if(target_kind.node_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                {
                    return validate_global_declaration_expression(
                        state, target_children[0]);
                }

                if(target_kind.node_kind == AstNodeKind::EXPRESSION_BINARY &&
                   target_kind.operator_kind == AstOperatorKind::SUBSCRIPT)
                {
                    CL_TRY(validate_global_declaration_expression(
                        state, target_children[0]));
                    return validate_global_declaration_expression(
                        state, target_children[1]);
                }

                return validate_global_declaration_expression(state,
                                                              target_idx);
            }

            Expected<void>
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
                            CL_TRY(declare_global_name(state,
                                                       av.constants[name_idx]));
                        }
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_FUNCTION_DEF:
                        {
                            AstChildren param_children =
                                signature_parameter_nodes(av, children[0]);
                            for(int32_t param_idx: param_children)
                            {
                                for(int32_t default_idx: av.children[param_idx])
                                {
                                    CL_TRY(
                                        validate_global_declaration_expression(
                                            state, default_idx));
                                }
                            }
                            mark_global_validation_assignment(
                                state, av.constants[node_idx]);
                            return Expected<void>::ok();
                        }

                    case AstNodeKind::STATEMENT_CLASS_DEF:
                        for(int32_t base_idx: av.children[children[0]])
                        {
                            CL_TRY(validate_global_declaration_expression(
                                state, base_idx));
                        }
                        mark_global_validation_assignment(
                            state, av.constants[node_idx]);
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_IMPORT:
                        for(int32_t alias_idx: children)
                        {
                            mark_global_validation_assignment(
                                state, av.constants[av.children[alias_idx][0]]);
                        }
                        return Expected<void>::ok();

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
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_ASSIGN:
                    case AstNodeKind::EXPRESSION_ASSIGN:
                        {
                            int32_t lhs_idx = children[0];
                            if(kind.operator_kind != AstOperatorKind::NOP)
                            {
                                CL_TRY(
                                    validate_global_declaration_assignment_target(
                                        state, lhs_idx, true));
                            }
                            CL_TRY(validate_global_declaration_expression(
                                state, children[1]));
                            if(kind.operator_kind == AstOperatorKind::NOP)
                            {
                                CL_TRY(
                                    validate_global_declaration_assignment_target(
                                        state, lhs_idx, false));
                            }
                            return Expected<void>::ok();
                        }

                    case AstNodeKind::STATEMENT_ANN_ASSIGN:
                        if(av.kinds[children[0]].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            CL_TRY(mark_global_validation_annotation(
                                state, av.constants[children[0]]));
                        }
                        else
                        {
                            CL_TRY(
                                validate_global_declaration_assignment_target(
                                    state, children[0], false));
                        }
                        if(children.size() == 3)
                        {
                            CL_TRY(validate_global_declaration_expression(
                                state, children[2]));
                        }
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_DEL:
                        for(int32_t target_idx: children)
                        {
                            CL_TRY(
                                validate_global_declaration_assignment_target(
                                    state, target_idx, false));
                        }
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_FOR:
                        CL_TRY(validate_global_declaration_expression(
                            state, children[1]));
                        CL_TRY(validate_global_declaration_assignment_target(
                            state, children[0], false));
                        CL_TRY(validate_global_declarations_in_node(
                            state, children[2]));
                        if(children.size() == 4)
                        {
                            CL_TRY(validate_global_declarations_in_node(
                                state, children[3]));
                        }
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_WITH:
                        for(size_t child_offset = 0;
                            child_offset + 1 < children.size(); ++child_offset)
                        {
                            CL_TRY(validate_global_declarations_in_node(
                                state, children[child_offset]));
                        }
                        return validate_global_declarations_in_node(
                            state, children.back());

                    case AstNodeKind::WITH_ITEM:
                        CL_TRY(validate_global_declaration_expression(
                            state, children[0]));
                        if(children.size() == 2)
                        {
                            CL_TRY(
                                validate_global_declaration_assignment_target(
                                    state, children[1], false));
                        }
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_EXCEPT_HANDLER:
                        if(handler_has_type(children))
                        {
                            CL_TRY(validate_global_declaration_expression(
                                state, handler_type_idx(children)));
                        }
                        if(handler_has_name(children))
                        {
                            CL_TRY(
                                validate_global_declaration_assignment_target(
                                    state, handler_name_idx(children), false));
                        }
                        return validate_global_declarations_in_node(
                            state, handler_body_idx(children));

                    case AstNodeKind::STATEMENT_SEQUENCE:
                        for(int32_t child_idx: children)
                        {
                            CL_TRY(validate_global_declarations_in_node(
                                state, child_idx));
                        }
                        return Expected<void>::ok();

                    case AstNodeKind::STATEMENT_BREAK:
                    case AstNodeKind::STATEMENT_CONTINUE:
                        return Expected<void>::ok();

                    default:
                        if(is_expression(kind.node_kind))
                        {
                            return validate_global_declaration_expression(
                                state, node_idx);
                        }
                        for(int32_t child_idx: children)
                        {
                            CL_TRY(validate_global_declarations_in_node(
                                state, child_idx));
                        }
                        return Expected<void>::ok();
                }
            }

            Expected<void>
            validate_global_declarations(CodegenMode mode, int32_t body_idx,
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

                return validate_global_declarations_in_node(state, body_idx);
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
                    case AstNodeKind::STATEMENT_BREAK:
                    case AstNodeKind::STATEMENT_CONTINUE:
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

                    case AstNodeKind::STATEMENT_BREAK:
                    case AstNodeKind::STATEMENT_CONTINUE:
                        return;

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

            LocalTouches
            empty_local_touches(const AnalysisState &analysis) const
            {
                return LocalTouches{
                    std::vector<bool>(analysis.result.bindings.size(), false),
                    std::vector<bool>(analysis.result.bindings.size(), false)};
            }

            void merge_local_touches(LocalTouches &target,
                                     const LocalTouches &source) const
            {
                assert(target.assigned.size() == source.assigned.size());
                assert(target.deleted.size() == source.deleted.size());
                for(size_t idx = 0; idx < target.assigned.size(); ++idx)
                {
                    target.assigned[idx] =
                        target.assigned[idx] || source.assigned[idx];
                    target.deleted[idx] =
                        target.deleted[idx] || source.deleted[idx];
                }
            }

            void mark_touch_assigned(const AnalysisState &analysis,
                                     LocalTouches &touches, Value name) const
            {
                int32_t binding_idx = find_binding_idx(analysis, name);
                if(binding_idx >= 0 &&
                   analysis.result.bindings[size_t(binding_idx)].scope ==
                       BindingScope::Local)
                {
                    touches.assigned[size_t(binding_idx)] = true;
                }
            }

            void mark_touch_deleted(const AnalysisState &analysis,
                                    LocalTouches &touches, Value name) const
            {
                int32_t binding_idx = find_binding_idx(analysis, name);
                if(binding_idx >= 0 &&
                   analysis.result.bindings[size_t(binding_idx)].scope ==
                       BindingScope::Local)
                {
                    touches.deleted[size_t(binding_idx)] = true;
                }
            }

            LocalTouches collect_local_touches(const AnalysisState &analysis,
                                               int32_t node_idx) const
            {
                LocalTouches touches = empty_local_touches(analysis);
                collect_local_touches_into(analysis, node_idx, touches);
                return touches;
            }

            void collect_local_touches_into(const AnalysisState &analysis,
                                            int32_t node_idx,
                                            LocalTouches &touches) const
            {
                AstKind kind = av.kinds[node_idx];
                AstChildren children = av.children[node_idx];

                switch(kind.node_kind)
                {
                    case AstNodeKind::STATEMENT_FUNCTION_DEF:
                    case AstNodeKind::STATEMENT_CLASS_DEF:
                        mark_touch_assigned(analysis, touches,
                                            av.constants[node_idx]);
                        return;

                    case AstNodeKind::STATEMENT_IMPORT:
                        for(int32_t alias_idx: children)
                        {
                            mark_touch_assigned(
                                analysis, touches,
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
                            mark_touch_assigned(
                                analysis, touches,
                                av.constants[av.children[alias_idx][0]]);
                        }
                        return;

                    case AstNodeKind::STATEMENT_ASSIGN:
                    case AstNodeKind::EXPRESSION_ASSIGN:
                        if(av.kinds[children[0]].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            mark_touch_assigned(analysis, touches,
                                                av.constants[children[0]]);
                        }
                        break;

                    case AstNodeKind::STATEMENT_ANN_ASSIGN:
                        if(children.size() == 3 &&
                           av.kinds[children[0]].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            mark_touch_assigned(analysis, touches,
                                                av.constants[children[0]]);
                        }
                        if(children.size() == 3)
                        {
                            collect_local_touches_into(analysis, children[2],
                                                       touches);
                        }
                        if(!ann_assign_is_simple(node_idx))
                        {
                            collect_local_touches_into(analysis, children[0],
                                                       touches);
                        }
                        return;

                    case AstNodeKind::STATEMENT_DEL:
                        for(int32_t target_idx: children)
                        {
                            if(av.kinds[target_idx].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                mark_touch_deleted(analysis, touches,
                                                   av.constants[target_idx]);
                            }
                            else
                            {
                                collect_local_touches_into(analysis, target_idx,
                                                           touches);
                            }
                        }
                        return;

                    case AstNodeKind::STATEMENT_FOR:
                        if(av.kinds[children[0]].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            mark_touch_assigned(analysis, touches,
                                                av.constants[children[0]]);
                        }
                        break;

                    case AstNodeKind::WITH_ITEM:
                        if(children.size() == 2 &&
                           av.kinds[children[1]].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            mark_touch_assigned(analysis, touches,
                                                av.constants[children[1]]);
                        }
                        break;

                    case AstNodeKind::STATEMENT_EXCEPT_HANDLER:
                        if(handler_has_name(children))
                        {
                            mark_touch_assigned(
                                analysis, touches,
                                av.constants[handler_name_idx(children)]);
                        }
                        break;

                    case AstNodeKind::STATEMENT_BREAK:
                    case AstNodeKind::STATEMENT_CONTINUE:
                        return;

                    default:
                        break;
                }

                for(int32_t child_idx: children)
                {
                    collect_local_touches_into(analysis, child_idx, touches);
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

            FlowState
            conservative_flow_state(const AnalysisState &analysis) const
            {
                FlowState state;
                state.local_presence.resize(analysis.result.bindings.size(),
                                            Presence::Maybe);
                state.may_be_entry_value.resize(analysis.result.bindings.size(),
                                                true);
                return state;
            }

            void mark_binding_maybe(FlowState &state, size_t binding_idx) const
            {
                state.local_presence[binding_idx] = Presence::Maybe;
            }

            void degrade_deleted_to_maybe(FlowState &state,
                                          const LocalTouches &touches,
                                          int32_t except_binding_idx = -1) const
            {
                for(size_t idx = 0; idx < touches.deleted.size(); ++idx)
                {
                    if(touches.deleted[idx] &&
                       int32_t(idx) != except_binding_idx)
                    {
                        mark_binding_maybe(state, idx);
                    }
                }
            }

            void degrade_touched_to_maybe(FlowState &state,
                                          const LocalTouches &touches) const
            {
                for(size_t idx = 0; idx < touches.assigned.size(); ++idx)
                {
                    if(touches.assigned[idx] || touches.deleted[idx])
                    {
                        mark_binding_maybe(state, idx);
                    }
                }
            }

            void apply_loop_exit_conservatism(FlowState &state,
                                              const LocalTouches &touches) const
            {
                for(size_t idx = 0; idx < touches.assigned.size(); ++idx)
                {
                    if(touches.deleted[idx])
                    {
                        mark_binding_maybe(state, idx);
                        continue;
                    }
                    if(touches.assigned[idx] &&
                       state.local_presence[idx] != Presence::Present)
                    {
                        mark_binding_maybe(state, idx);
                    }
                }
            }

            void merge_fallthrough(std::optional<FlowState> &target,
                                   const FlowState &source) const
            {
                target = target.has_value() ? merge_flow_states(*target, source)
                                            : std::optional<FlowState>(source);
            }

            void append_abrupt_exits(FlowSummary &target,
                                     const FlowSummary &source) const
            {
                target.breaks.insert(target.breaks.end(), source.breaks.begin(),
                                     source.breaks.end());
                target.continues.insert(target.continues.end(),
                                        source.continues.begin(),
                                        source.continues.end());
            }

            void
            degrade_summary_touched_to_maybe(FlowSummary &summary,
                                             const LocalTouches &touches) const
            {
                if(summary.fallthrough.has_value())
                {
                    degrade_touched_to_maybe(*summary.fallthrough, touches);
                }
                for(FlowState &state: summary.breaks)
                {
                    degrade_touched_to_maybe(state, touches);
                }
                for(FlowState &state: summary.continues)
                {
                    degrade_touched_to_maybe(state, touches);
                }
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
                    analyze_child_as_expression(analysis, target_idx, state);
                }
            }

            FlowSummary fallthrough_summary(FlowState state) const
            {
                FlowSummary summary;
                summary.fallthrough = state;
                return summary;
            }

            FlowSummary analyze_sequence(AnalysisState &analysis,
                                         AstChildren children, FlowState state)
            {
                FlowSummary result;
                std::optional<FlowState> fallthrough = state;
                for(int32_t child_idx: children)
                {
                    bool reachable = fallthrough.has_value();
                    FlowState child_state =
                        reachable ? *fallthrough
                                  : conservative_flow_state(analysis);
                    FlowSummary child =
                        analyze_flow_node(analysis, child_idx, child_state);
                    if(reachable)
                    {
                        append_abrupt_exits(result, child);
                    }
                    fallthrough = reachable ? child.fallthrough
                                            : std::optional<FlowState>();
                }
                result.fallthrough = fallthrough;
                return result;
            }

            FlowSummary analyze_child_as_expression(AnalysisState &analysis,
                                                    int32_t node_idx,
                                                    FlowState &state)
            {
                FlowSummary summary =
                    analyze_flow_node(analysis, node_idx, state);
                state = summary.fallthrough.has_value()
                            ? *summary.fallthrough
                            : conservative_flow_state(analysis);
                return summary;
            }

            FlowSummary analyze_if_statement(AnalysisState &analysis,
                                             AstChildren children,
                                             FlowState state)
            {
                FlowSummary result;
                std::optional<FlowState> next_condition = state;
                for(size_t idx = 0; idx + 1 < children.size(); idx += 2)
                {
                    bool condition_reachable = next_condition.has_value();
                    FlowState condition_state =
                        condition_reachable ? *next_condition
                                            : conservative_flow_state(analysis);
                    FlowSummary condition = analyze_flow_node(
                        analysis, children[idx], condition_state);
                    if(condition_reachable)
                    {
                        append_abrupt_exits(result, condition);
                    }

                    bool branch_reachable = condition_reachable &&
                                            condition.fallthrough.has_value();
                    FlowState branch_state =
                        branch_reachable ? *condition.fallthrough
                                         : conservative_flow_state(analysis);
                    FlowSummary branch = analyze_flow_node(
                        analysis, children[idx + 1], branch_state);
                    if(branch_reachable)
                    {
                        append_abrupt_exits(result, branch);
                        if(branch.fallthrough.has_value())
                        {
                            merge_fallthrough(result.fallthrough,
                                              *branch.fallthrough);
                        }
                    }

                    next_condition = condition_reachable
                                         ? condition.fallthrough
                                         : std::optional<FlowState>();
                }

                if(children.size() & 1)
                {
                    bool else_reachable = next_condition.has_value();
                    FlowState else_state =
                        else_reachable ? *next_condition
                                       : conservative_flow_state(analysis);
                    FlowSummary else_summary = analyze_flow_node(
                        analysis, children.back(), else_state);
                    if(else_reachable)
                    {
                        append_abrupt_exits(result, else_summary);
                        if(else_summary.fallthrough.has_value())
                        {
                            merge_fallthrough(result.fallthrough,
                                              *else_summary.fallthrough);
                        }
                    }
                }
                else if(next_condition.has_value())
                {
                    merge_fallthrough(result.fallthrough, *next_condition);
                }
                return result;
            }

            FlowSummary analyze_while_statement(AnalysisState &analysis,
                                                AstChildren children,
                                                FlowState state)
            {
                LocalTouches body_touches =
                    collect_local_touches(analysis, children[1]);
                FlowState condition_state = state;
                degrade_deleted_to_maybe(condition_state, body_touches);

                FlowSummary condition =
                    analyze_flow_node(analysis, children[0], condition_state);
                FlowSummary result;
                append_abrupt_exits(result, condition);

                FlowState body_state = condition.fallthrough.has_value()
                                           ? *condition.fallthrough
                                           : conservative_flow_state(analysis);
                degrade_deleted_to_maybe(body_state, body_touches);
                FlowSummary body =
                    analyze_flow_node(analysis, children[1], body_state);

                FlowState normal_exit = condition.fallthrough.has_value()
                                            ? *condition.fallthrough
                                            : condition_state;
                apply_loop_exit_conservatism(normal_exit, body_touches);
                if(children.size() == 3)
                {
                    FlowSummary else_summary =
                        analyze_flow_node(analysis, children[2], normal_exit);
                    append_abrupt_exits(result, else_summary);
                    result.fallthrough = else_summary.fallthrough;
                }
                else
                {
                    result.fallthrough = normal_exit;
                }

                for(FlowState break_state: body.breaks)
                {
                    degrade_deleted_to_maybe(break_state, body_touches);
                    merge_fallthrough(result.fallthrough, break_state);
                }
                return result;
            }

            FlowSummary analyze_for_statement(AnalysisState &analysis,
                                              AstChildren children,
                                              FlowState state)
            {
                int32_t target_idx = children[0];
                int32_t iterable_idx = children[1];
                int32_t body_idx = children[2];
                FlowSummary result;

                FlowSummary iterable =
                    analyze_flow_node(analysis, iterable_idx, state);
                append_abrupt_exits(result, iterable);
                FlowState iterated_state =
                    iterable.fallthrough.has_value()
                        ? *iterable.fallthrough
                        : conservative_flow_state(analysis);

                LocalTouches body_touches =
                    collect_local_touches(analysis, body_idx);
                int32_t target_binding_idx = -1;
                if(av.kinds[target_idx].node_kind ==
                   AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                {
                    target_binding_idx =
                        find_binding_idx(analysis, av.constants[target_idx]);
                    if(target_binding_idx >= 0)
                    {
                        body_touches.assigned[size_t(target_binding_idx)] =
                            true;
                    }
                }

                FlowState body_state = iterated_state;
                annotate_for_target_store(analysis, target_idx, body_state);
                degrade_deleted_to_maybe(body_state, body_touches,
                                         target_binding_idx);
                FlowSummary body =
                    analyze_flow_node(analysis, body_idx, body_state);

                FlowState normal_exit = iterated_state;
                apply_loop_exit_conservatism(normal_exit, body_touches);
                if(children.size() == 4)
                {
                    FlowSummary else_summary =
                        analyze_flow_node(analysis, children[3], normal_exit);
                    append_abrupt_exits(result, else_summary);
                    result.fallthrough = else_summary.fallthrough;
                }
                else
                {
                    result.fallthrough = normal_exit;
                }

                for(FlowState break_state: body.breaks)
                {
                    degrade_deleted_to_maybe(break_state, body_touches);
                    merge_fallthrough(result.fallthrough, break_state);
                }
                return result;
            }

            FlowSummary analyze_try_except_statement(AnalysisState &analysis,
                                                     AstChildren children,
                                                     size_t end_child_offset,
                                                     FlowState state)
            {
                int32_t body_idx = children[0];
                int32_t else_body_idx = -1;
                size_t handler_end_child_offset = end_child_offset;
                if(try_has_else(children, end_child_offset))
                {
                    else_body_idx =
                        try_else_body_idx(children, end_child_offset);
                    --handler_end_child_offset;
                }

                LocalTouches all_touches = empty_local_touches(analysis);
                LocalTouches protected_touches =
                    collect_local_touches(analysis, body_idx);
                merge_local_touches(all_touches, protected_touches);

                FlowSummary result =
                    analyze_flow_node(analysis, body_idx, state);
                if(else_body_idx >= 0)
                {
                    LocalTouches else_touches =
                        collect_local_touches(analysis, else_body_idx);
                    merge_local_touches(all_touches, else_touches);
                    if(result.fallthrough.has_value())
                    {
                        FlowSummary else_summary = analyze_flow_node(
                            analysis, else_body_idx, *result.fallthrough);
                        result.fallthrough = else_summary.fallthrough;
                        append_abrupt_exits(result, else_summary);
                    }
                    else
                    {
                        FlowState unreachable =
                            conservative_flow_state(analysis);
                        (void)analyze_flow_node(analysis, else_body_idx,
                                                unreachable);
                    }
                }

                FlowState handler_entry = state;
                degrade_touched_to_maybe(handler_entry, protected_touches);
                for(size_t child_offset = 1;
                    child_offset < handler_end_child_offset; ++child_offset)
                {
                    int32_t handler_idx = children[child_offset];
                    AstChildren handler_children = av.children[handler_idx];
                    LocalTouches handler_touches =
                        collect_local_touches(analysis, handler_idx);
                    merge_local_touches(all_touches, handler_touches);

                    FlowState handler_state = handler_entry;
                    if(handler_has_type(handler_children))
                    {
                        analyze_child_as_expression(
                            analysis, handler_type_idx(handler_children),
                            handler_state);
                    }
                    if(handler_has_name(handler_children))
                    {
                        annotate_for_target_store(
                            analysis, handler_name_idx(handler_children),
                            handler_state);
                    }
                    FlowSummary handler = analyze_flow_node(
                        analysis, handler_body_idx(handler_children),
                        handler_state);
                    append_abrupt_exits(result, handler);
                    if(handler.fallthrough.has_value())
                    {
                        merge_fallthrough(result.fallthrough,
                                          *handler.fallthrough);
                    }
                }

                degrade_summary_touched_to_maybe(result, all_touches);
                return result;
            }

            FlowSummary analyze_try_statement(AnalysisState &analysis,
                                              AstChildren children,
                                              FlowState state)
            {
                if(!try_has_finally(children))
                {
                    return analyze_try_except_statement(analysis, children,
                                                        children.size(), state);
                }

                int32_t finally_body_idx = try_finally_body_idx(children);
                LocalTouches all_touches = empty_local_touches(analysis);
                LocalTouches protected_touches = empty_local_touches(analysis);
                for(size_t child_offset = 0; child_offset + 1 < children.size();
                    ++child_offset)
                {
                    merge_local_touches(protected_touches,
                                        collect_local_touches(
                                            analysis, children[child_offset]));
                }
                merge_local_touches(all_touches, protected_touches);

                FlowSummary protected_summary;
                if(children.size() == 2)
                {
                    protected_summary =
                        analyze_flow_node(analysis, children[0], state);
                }
                else
                {
                    protected_summary = analyze_try_except_statement(
                        analysis, children, children.size() - 1, state);
                }

                LocalTouches finally_touches =
                    collect_local_touches(analysis, finally_body_idx);
                merge_local_touches(all_touches, finally_touches);

                FlowState finally_entry = state;
                degrade_touched_to_maybe(finally_entry, protected_touches);
                FlowSummary finally_summary = analyze_flow_node(
                    analysis, finally_body_idx, finally_entry);

                FlowSummary result;
                if(finally_summary.fallthrough.has_value())
                {
                    result = protected_summary;
                    append_abrupt_exits(result, finally_summary);
                }
                else
                {
                    result.breaks = finally_summary.breaks;
                    result.continues = finally_summary.continues;
                }
                degrade_summary_touched_to_maybe(result, all_touches);
                return result;
            }

            FlowSummary analyze_with_statement(AnalysisState &analysis,
                                               AstChildren children,
                                               FlowState state)
            {
                LocalTouches touches = empty_local_touches(analysis);
                for(int32_t child_idx: children)
                {
                    merge_local_touches(
                        touches, collect_local_touches(analysis, child_idx));
                }
                FlowSummary result =
                    analyze_sequence(analysis, children, state);
                FlowState suppressed_exception_exit = state;
                degrade_touched_to_maybe(suppressed_exception_exit, touches);
                merge_fallthrough(result.fallthrough,
                                  suppressed_exception_exit);
                degrade_summary_touched_to_maybe(result, touches);
                return result;
            }

            FlowSummary analyze_flow_node(AnalysisState &analysis,
                                          int32_t node_idx, FlowState state)
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
                        return fallthrough_summary(state);

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
                                analyze_child_as_expression(analysis,
                                                            children[1], state);
                                annotate_write(lhs_idx);
                                return fallthrough_summary(state);
                            }

                            analyze_child_as_expression(analysis, lhs_idx,
                                                        state);
                            analyze_child_as_expression(analysis, children[1],
                                                        state);
                            return fallthrough_summary(state);
                        }

                    case AstNodeKind::STATEMENT_IMPORT:
                        for(int32_t alias_idx: children)
                        {
                            annotate_write(av.children[alias_idx][0]);
                        }
                        return fallthrough_summary(state);

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
                        return fallthrough_summary(state);

                    case AstNodeKind::STATEMENT_ANN_ASSIGN:
                        if(children.size() == 3)
                        {
                            if(av.kinds[children[0]].node_kind ==
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                            {
                                analyze_child_as_expression(analysis,
                                                            children[2], state);
                                annotate_write(children[0]);
                                return fallthrough_summary(state);
                            }
                            analyze_child_as_expression(analysis, children[0],
                                                        state);
                            analyze_child_as_expression(analysis, children[2],
                                                        state);
                        }
                        else if(!ann_assign_is_simple(node_idx))
                        {
                            analyze_child_as_expression(analysis, children[0],
                                                        state);
                        }
                        return fallthrough_summary(state);

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
                                analyze_child_as_expression(analysis,
                                                            target_idx, state);
                            }
                        }
                        return fallthrough_summary(state);

                    case AstNodeKind::STATEMENT_SEQUENCE:
                    case AstNodeKind::STATEMENT_EXPRESSION:
                        return analyze_sequence(analysis, children, state);

                    case AstNodeKind::STATEMENT_IF:
                        return analyze_if_statement(analysis, children, state);

                    case AstNodeKind::STATEMENT_WHILE:
                        return analyze_while_statement(analysis, children,
                                                       state);

                    case AstNodeKind::STATEMENT_FOR:
                        return analyze_for_statement(analysis, children, state);

                    case AstNodeKind::STATEMENT_WITH:
                        return analyze_with_statement(analysis, children,
                                                      state);

                    case AstNodeKind::WITH_ITEM:
                        analyze_child_as_expression(analysis, children[0],
                                                    state);
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
                                analyze_child_as_expression(analysis,
                                                            target_idx, state);
                            }
                        }
                        return fallthrough_summary(state);

                    case AstNodeKind::STATEMENT_TRY:
                        return analyze_try_statement(analysis, children, state);

                    case AstNodeKind::STATEMENT_FUNCTION_DEF:
                        {
                            AstChildren param_children =
                                signature_parameter_nodes(av, children[0]);
                            for(int32_t param_idx: param_children)
                            {
                                for(int32_t default_idx: av.children[param_idx])
                                {
                                    analyze_child_as_expression(
                                        analysis, default_idx, state);
                                }
                            }
                            annotate_named_definition(node_idx);
                            return fallthrough_summary(state);
                        }

                    case AstNodeKind::STATEMENT_CLASS_DEF:
                        for(int32_t base_idx: av.children[children[0]])
                        {
                            analyze_child_as_expression(analysis, base_idx,
                                                        state);
                        }
                        annotate_named_definition(node_idx);
                        return fallthrough_summary(state);

                    case AstNodeKind::STATEMENT_RETURN:
                        if(!children.empty())
                        {
                            analyze_child_as_expression(analysis, children[0],
                                                        state);
                        }
                        return FlowSummary{};

                    case AstNodeKind::STATEMENT_RAISE:
                        if(!children.empty())
                        {
                            analyze_child_as_expression(analysis, children[0],
                                                        state);
                        }
                        return FlowSummary{};

                    case AstNodeKind::STATEMENT_ASSERT:
                        analyze_child_as_expression(analysis, children[0],
                                                    state);
                        if(children.size() == 2)
                        {
                            FlowState message_state = state;
                            (void)analyze_flow_node(analysis, children[1],
                                                    message_state);
                        }
                        return fallthrough_summary(state);

                    case AstNodeKind::STATEMENT_BREAK:
                        {
                            FlowSummary summary;
                            summary.breaks.push_back(state);
                            return summary;
                        }

                    case AstNodeKind::STATEMENT_CONTINUE:
                        {
                            FlowSummary summary;
                            summary.continues.push_back(state);
                            return summary;
                        }

                    case AstNodeKind::STATEMENT_PASS:
                    case AstNodeKind::STATEMENT_GLOBAL:
                    case AstNodeKind::STATEMENT_NONLOCAL:
                        return fallthrough_summary(state);

                    default:
                        for(int32_t child_idx: children)
                        {
                            analyze_child_as_expression(analysis, child_idx,
                                                        state);
                        }
                        return fallthrough_summary(state);
                }
            }
        };
    }  // namespace

    Expected<ScopeAnalysis> analyze_code_object_scope(
        const AstVector &av, CodeObjectBuilder *target_code_obj,
        int32_t body_idx, CodegenMode mode, AstChildren param_children)
    {
        ScopeAnalyzer analyzer(av, target_code_obj);
        return analyzer.run(body_idx, mode, param_children);
    }

}  // namespace cl
