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
    class Tuple;

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

    [[nodiscard]] Expected<void>
    validate_expanded_call_keywords(TValue<Dict> keywords);

    // Construct collectors before writing any outgoing parameter slots.
    [[nodiscard]] Expected<void> apply_expanded_call_binding_plan(
        ThreadState *thread, TValue<Function> function,
        TValue<Tuple> positional, TValue<Dict> keywords,
        const ExpandedCallBindingPlan &plan, Value *destination_fp);
}  // namespace cl

#endif  // CL_EXPANDED_CALL_H
