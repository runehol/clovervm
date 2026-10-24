#include "runtime/expanded_call.h"

#include "builtin_types/dict.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/function.h"
#include "object_model/owned.h"
#include "runtime/thread_state.h"
#include <absl/container/inlined_vector.h>
#include <algorithm>
#include <cassert>
#include <utility>

namespace cl
{
    Expected<void> validate_expanded_call_keywords(TValue<Dict> keywords)
    {
        for(Dict::EntryView entry: *keywords.extract())
        {
            if(!can_convert_to<String>(entry.key))
            {
                return Expected<void>::raise_exception(
                    L"TypeError", L"keywords must be strings");
            }
        }
        return Expected<void>::ok();
    }

    Expected<ExpandedCallBindingPlan> build_expanded_call_binding_plan(
        ThreadState *thread, TValue<Function> function,
        uint32_t positional_count, TValue<Dict> keywords)
    {
        assert(keywords.extract()->get_shape() ==
                   thread->get_exact_dict_string_key_shape() ||
               keywords.extract()->get_shape() ==
                   thread->get_exact_dict_general_shape());

        // Validate the whole transport before any equality callback can run.
        CL_TRY(validate_expanded_call_keywords(keywords));

        ExpandedCallBindingPlan plan;
        plan.keyword_destinations.reserve(keywords.extract()->size());
        const FunctionSignature signature =
            function.extract()->call_signature.function;
        absl::InlinedVector<bool, 16> filled(signature.n_parameters, false);
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

    Expected<void> apply_expanded_call_binding_plan(
        ThreadState *thread, TValue<Function> function,
        ExpandedCallPositionalArguments positional, TValue<Dict> keywords,
        const ExpandedCallBindingPlan &plan, Value *destination_fp)
    {
        const FunctionSignature signature =
            function.extract()->call_signature.function;
        const size_t positional_fills = std::min(
            positional.size(), size_t(signature.n_positional_parameters));
        Owned<Optional<TValue<Tuple>>> varargs;
        if(signature.has_varargs())
        {
            if(positional_fills == (positional.has_prefix() ? 1u : 0u))
            {
                varargs = Optional<TValue<Tuple>>::some(positional.tuple);
            }
            else
            {
                varargs = Optional<TValue<Tuple>>::some(
                    thread->make_object_value<Tuple>(positional.size() -
                                                     positional_fills));
                for(size_t idx = positional_fills; idx < positional.size();
                    ++idx)
                {
                    varargs.value()
                        .value()
                        .extract()
                        ->initialize_item_unchecked(
                            idx - positional_fills,
                            positional.item_unchecked(idx));
                }
            }
        }

        Owned<Optional<TValue<Dict>>> collector;
        if(plan.kwargs_action == KwargsAction::ReuseTransport)
        {
            collector = Optional<TValue<Dict>>::some(keywords);
        }
        else if(plan.kwargs_action == KwargsAction::BuildLeftovers)
        {
            collector =
                Optional<TValue<Dict>>::some(thread->make_object_value<Dict>());
            size_t keyword_idx = 0;
            for(size_t entry_idx = 0;
                entry_idx < keywords.extract()->entry_storage_size();
                ++entry_idx)
            {
                Dict::EntryView entry;
                if(!keywords.extract()->entry_at(entry_idx, entry))
                {
                    continue;
                }
                assert(keyword_idx < plan.keyword_destinations.size());
                if(plan.keyword_destinations[keyword_idx++] ==
                   ExpandedCallBindingPlan::UnmatchedKeyword)
                {
                    // Insertion may call subclass protocols; the dict and
                    // current entry must survive nested interpreter entry.
                    Owned<Value> key(entry.key);
                    Owned<Value> value(entry.value);
                    CL_TRY(collector.value().value().extract()->set_item(
                        thread, key.value(), value.value()));
                }
            }
            assert(keyword_idx == plan.keyword_destinations.size());
        }

        // All callback-capable work is complete. The outgoing window can now
        // be populated without a nested call overwriting partial arguments.
        CodeObject *code = function.extract()->code_object.extract();
        for(uint32_t idx = 0; idx < positional_fills; ++idx)
        {
            destination_fp[code->encode_reg(idx)] =
                positional.item_unchecked(idx);
        }
        size_t keyword_idx = 0;
        for(Dict::EntryView entry: *keywords.extract())
        {
            assert(keyword_idx < plan.keyword_destinations.size());
            uint32_t destination = plan.keyword_destinations[keyword_idx++];
            if(destination != ExpandedCallBindingPlan::UnmatchedKeyword)
            {
                destination_fp[code->encode_reg(destination)] = entry.value;
            }
        }
        assert(keyword_idx == plan.keyword_destinations.size());
        for(uint32_t slot: plan.default_slots)
        {
            destination_fp[code->encode_reg(slot)] =
                function.extract()
                    ->default_parameters.value()
                    .value()
                    .extract()
                    ->item_unchecked(slot - signature.first_default_slot);
        }
        if(signature.has_varargs())
        {
            destination_fp[code->encode_reg(
                signature.n_positional_parameters)] =
                varargs.value().value().raw_value();
        }
        if(signature.has_kwargs())
        {
            destination_fp[code->encode_reg(signature.n_parameters - 1)] =
                collector.value().value().raw_value();
        }
        return Expected<void>::ok();
    }
}  // namespace cl
