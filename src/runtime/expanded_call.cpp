#include "runtime/expanded_call.h"

#include "builtin_types/dict.h"
#include "builtin_types/str.h"
#include "object_model/function.h"
#include "object_model/owned.h"
#include "runtime/thread_state.h"
#include <algorithm>
#include <cassert>
#include <utility>

namespace cl
{
    Expected<ExpandedCallBindingPlan> build_expanded_call_binding_plan(
        ThreadState *thread, TValue<Function> function,
        uint32_t positional_count, TValue<Dict> keywords)
    {
        assert(keywords.extract()->get_shape() ==
                   thread->get_exact_dict_string_key_shape() ||
               keywords.extract()->get_shape() ==
                   thread->get_exact_dict_general_shape());

        // Validate the whole transport before any equality callback can run.
        for(Dict::EntryView entry: *keywords.extract())
        {
            if(!can_convert_to<String>(entry.key))
            {
                return Expected<ExpandedCallBindingPlan>::raise_exception(
                    L"TypeError", L"keywords must be strings");
            }
        }

        ExpandedCallBindingPlan plan;
        plan.keyword_destinations.reserve(keywords.extract()->size());
        const FunctionSignature signature =
            function.extract()->call_signature.function;
        std::vector<bool> filled(signature.n_parameters, false);
        const uint32_t positional_fills =
            std::min(positional_count, signature.n_positional_parameters);
        for(uint32_t idx = 0; idx < positional_fills; ++idx)
        {
            filled[idx] = true;
        }

        const FunctionKeywordRemap &remap =
            function.extract()->code_object.extract()->function_keyword_remap;
        bool matched_keyword = false;
        // Retain only a numeric traversal position across callbacks, never a
        // pointer into dict storage. Keep the current key alive independently.
        for(size_t entry_idx = 0;
            entry_idx < keywords.extract()->entry_storage_size(); ++entry_idx)
        {
            Dict::EntryView entry;
            if(!keywords.extract()->entry_at(entry_idx, entry))
            {
                continue;
            }
            Owned<TValue<String>> key(
                TValue<String>::from_value_assumed(entry.key));
            const bool exact = is_exact_string(thread, key.raw_value());
            uint32_t destination = ExpandedCallBindingPlan::UnmatchedKeyword;
            for(size_t idx = 0; idx < remap.size(); ++idx)
            {
                TValue<String> name =
                    TValue<String>::from_value_assumed(remap.name_at(idx));
                bool equal = exact ? string_eq(name, key.value())
                                   : CL_TRY(thread->test_equal(
                                         name.raw_value(), key.raw_value()));
                if(equal)
                {
                    destination = remap.parameter_index_at(idx);
                    break;
                }
            }

            if(destination == ExpandedCallBindingPlan::UnmatchedKeyword)
            {
                if(!signature.has_kwargs())
                {
                    return Expected<ExpandedCallBindingPlan>::raise_exception(
                        L"TypeError", L"invalid keyword argument");
                }
            }
            else
            {
                assert(destination < signature.n_parameters);
                if(filled[destination])
                {
                    return Expected<ExpandedCallBindingPlan>::raise_exception(
                        L"TypeError", L"invalid keyword argument");
                }
                filled[destination] = true;
                matched_keyword = true;
            }
            plan.keyword_destinations.push_back(destination);
        }

        if(positional_count > signature.n_positional_parameters &&
           !signature.has_varargs())
        {
            return Expected<ExpandedCallBindingPlan>::raise_exception(
                L"TypeError", L"wrong number of arguments");
        }

        for(uint32_t idx = 0; idx < signature.n_parameters; ++idx)
        {
            if(filled[idx] ||
               (signature.has_varargs() &&
                idx == signature.n_positional_parameters) ||
               (signature.has_kwargs() && idx == signature.n_parameters - 1))
            {
                continue;
            }
            if(!function.extract()->has_default_for_parameter(idx))
            {
                return Expected<ExpandedCallBindingPlan>::raise_exception(
                    L"TypeError", L"wrong number of arguments");
            }
            plan.default_slots.push_back(idx);
        }

        if(signature.has_kwargs())
        {
            plan.kwargs_action = matched_keyword ? KwargsAction::BuildLeftovers
                                                 : KwargsAction::ReuseTransport;
        }
        return Expected<ExpandedCallBindingPlan>::ok(std::move(plan));
    }
}  // namespace cl
