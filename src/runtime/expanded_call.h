#ifndef CL_EXPANDED_CALL_H
#define CL_EXPANDED_CALL_H

#include "builtin_types/tuple.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cl
{
    class Dict;
    class Function;
    class String;
    class ThreadState;
    class Tuple;

    // Borrowed inputs, rooted by the caller. None is a valid receiver, so
    // absence uses the internal not_present marker instead.
    struct ExpandedCallPositionalArguments
    {
        TValue<Tuple> tuple;
        Value prefix = Value::not_present();

        bool has_prefix() const { return !prefix.is_not_present(); }
        size_t size() const
        {
            return tuple.extract()->size() + (has_prefix() ? 1 : 0);
        }
        Value item_unchecked(size_t index) const
        {
            if(has_prefix())
            {
                if(index == 0)
                    return prefix;
                --index;
            }
            return tuple.extract()->item_unchecked(index);
        }
    };

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

    struct ExpandedCallCachedPlan
    {
        uint32_t positional_count = 0;
        std::vector<Owned<TValue<String>>> keyword_names;
        ExpandedCallBindingPlan plan;
    };

    bool expanded_call_cached_plan_matches(ThreadState *thread,
                                           const ExpandedCallCachedPlan &cached,
                                           uint32_t positional_count,
                                           TValue<Dict> keywords);

    // Returns null for non-exact-string keys without consuming the local plan.
    std::unique_ptr<ExpandedCallCachedPlan>
    convert_to_expanded_call_cache_entry(ThreadState *thread,
                                         uint32_t positional_count,
                                         TValue<Dict> keywords,
                                         ExpandedCallBindingPlan &&plan);

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
        ExpandedCallPositionalArguments positional, TValue<Dict> keywords,
        const ExpandedCallBindingPlan &plan, Value *destination_fp);
}  // namespace cl

#endif  // CL_EXPANDED_CALL_H
