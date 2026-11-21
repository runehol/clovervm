#ifndef CL_CODEGEN_SCOPE_ANALYSIS_H
#define CL_CODEGEN_SCOPE_ANALYSIS_H

#include "compiler/ast.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cl
{
    enum class CodegenMode
    {
        Module,
        Class,
        Function
    };

    enum class BindingScope
    {
        Local,
        Cell,
        Free,
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
        // Required by checked accesses or GC-visible normal-flow joins.
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
        struct FreeBinding
        {
            uint32_t binding_index;
            uint32_t parent_binding_index;
        };

        CodegenMode mode = CodegenMode::Module;
        uint32_t n_slots = 0;
        std::vector<BindingInfo> bindings;
        std::vector<uint32_t> named_bindings_in_slot_order;
        std::vector<FreeBinding> free_bindings;
        AstChildren child_definitions;
        // Indexed by local slot, including parameter slots. These locals must
        // be copied when an evaluated operand is retained in a register.
        std::vector<bool> expression_assigned_locals;
    };

    struct CompilationScopeAnalysis
    {
        std::unordered_map<int32_t, ScopeAnalysis> scopes_by_body;
        std::vector<std::optional<NameAccessAnalysis>> loads;
        std::vector<std::optional<NameAccessAnalysis>> stores;
        std::vector<std::optional<NameAccessAnalysis>> deletes;

        explicit CompilationScopeAnalysis(size_t n_nodes)
            : loads(n_nodes), stores(n_nodes), deletes(n_nodes)
        {
        }
    };

    Expected<CompilationScopeAnalysis>
    analyze_compilation_scopes(const AstVector &av);

}  // namespace cl

#endif  // CL_CODEGEN_SCOPE_ANALYSIS_H
