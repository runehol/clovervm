#ifndef CL_CODEGEN_SCOPE_ANALYSIS_H
#define CL_CODEGEN_SCOPE_ANALYSIS_H

#include "ast.h"
#include "value.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cl
{
    class CodeObjectBuilder;

    enum class CodegenMode
    {
        Module,
        Class,
        Function
    };

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
        CodegenMode mode = CodegenMode::Module;
        std::vector<BindingInfo> bindings;
        std::vector<std::optional<NameAccessAnalysis>> loads;
        std::vector<std::optional<NameAccessAnalysis>> stores;
        std::vector<std::optional<NameAccessAnalysis>> deletes;

        ScopeAnalysis(CodegenMode _mode, size_t n_nodes)
            : mode(_mode), loads(n_nodes), stores(n_nodes), deletes(n_nodes)
        {
        }
    };

    ScopeAnalysis analyze_code_object_scope(const AstVector &av,
                                            CodeObjectBuilder *target_code_obj,
                                            int32_t body_idx, CodegenMode mode,
                                            AstChildren param_children = {});

}  // namespace cl

#endif  // CL_CODEGEN_SCOPE_ANALYSIS_H
