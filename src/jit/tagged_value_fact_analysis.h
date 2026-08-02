#ifndef CL_JIT_TAGGED_VALUE_FACT_ANALYSIS_H
#define CL_JIT_TAGGED_VALUE_FACT_ANALYSIS_H

#include "jit/instruction.h"
#include "jit/tagged_value_facts.h"

#include <absl/container/flat_hash_map.h>

#include <cstdint>

namespace cl::jit
{
    class ControlFlowGraph;

    class TaggedValueFactAnalysis
    {
    public:
        TaggedValueFactAnalysis(const TaggedValueFactAnalysis &) = delete;
        TaggedValueFactAnalysis &
        operator=(const TaggedValueFactAnalysis &) = delete;
        TaggedValueFactAnalysis(TaggedValueFactAnalysis &&) = default;
        TaggedValueFactAnalysis &
        operator=(TaggedValueFactAnalysis &&) = default;

        const TaggedValueSet &facts_of(ProgramValueRef value) const;

    private:
        friend class ControlFlowGraph;

        explicit TaggedValueFactAnalysis(const ControlFlowGraph &graph);

        uint64_t graph_generation() const { return graph_generation_; }

        uint64_t graph_generation_;
        absl::flat_hash_map<InstructionId, TaggedValueSet> facts_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_TAGGED_VALUE_FACT_ANALYSIS_H
