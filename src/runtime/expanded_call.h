#ifndef CL_EXPANDED_CALL_H
#define CL_EXPANDED_CALL_H

#include "object_model/typed_value.h"
#include <cstdint>
#include <vector>

namespace cl
{
    class Dict;
    class Function;
    class ThreadState;

    enum class KwargsAction : uint8_t
    {
        None,
        ReuseTransport,
        BuildLeftovers,
    };

    // Reference-free binding decisions shared by temporary and cached plans.
    struct ExpandedCallBindingPlan
    {
        static constexpr uint32_t UnmatchedKeyword = UINT32_MAX;

        std::vector<uint32_t> keyword_destinations;
        std::vector<uint32_t> default_slots;
        KwargsAction kwargs_action = KwargsAction::None;
    };

    // keywords is an exact, call-owned transport dict. The caller keeps it and
    // function rooted throughout construction and subsequent plan application.
    [[nodiscard]] Expected<ExpandedCallBindingPlan>
    build_expanded_call_binding_plan(ThreadState *thread,
                                     TValue<Function> function,
                                     uint32_t positional_count,
                                     TValue<Dict> keywords);
}  // namespace cl

#endif  // CL_EXPANDED_CALL_H
