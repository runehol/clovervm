#ifndef CL_JOIN_H
#define CL_JOIN_H

#include "builtin_types/list.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/owned.h"
#include "runtime/thread_state.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace cl
{
    // The caller validates the receiver and fully materializes nonexact
    // sequences before entry. item_view only inspects stored native payloads:
    // it must not allocate, mutate, enter Python, or poll a safepoint.
    template <typename Result, typename Char, typename ItemView>
    [[nodiscard]] Value
    join_sequence(ThreadState *thread, TValue<Result> separator_value,
                  Value sequence, ItemView item_view,
                  const wchar_t *expected_type, const wchar_t *overflow_message)
    {
        static_assert(std::is_same_v<decltype(item_view(sequence)),
                                     std::optional<std::span<const Char>>>);
        static_assert(std::is_same_v<decltype(Result::data[0]), Char &>);
        Owned<TValue<Result>> separator(separator_value);
        Owned<Value> items(sequence);
        const bool is_list = has_native_layout<List>(sequence);
        assert(is_list || has_native_layout<Tuple>(sequence));
        const size_t count = is_list ? sequence.get_ptr<List>()->size()
                                     : sequence.get_ptr<Tuple>()->size();
        auto item_at = [&](size_t index) {
            return is_list
                       ? items.value().get_ptr<List>()->item_unchecked(index)
                       : items.value().get_ptr<Tuple>()->item_unchecked(index);
        };
        if(count == 1)
        {
            Value item = item_at(0);
            if(has_native_layout<Result>(item) &&
               thread->class_of_value(item) ==
                   thread->class_for_native_layout(Result::native_layout))
                return item;
        }
        const size_t separator_size =
            size_t(separator.extract()->count.extract());
        // Conservative for Bytes' inline first byte; exact for String's
        // allocation formula, whose sizeof includes its terminator storage.
        const size_t limit =
            std::min(size_t(value_smi_max),
                     (std::numeric_limits<size_t>::max() - sizeof(Result)) /
                         sizeof(Char));
        size_t total = 0;
        for(size_t index = 0; index < count; ++index)
        {
            auto view = item_view(item_at(index));
            if(!view)
            {
                auto name = thread->class_of_value(item_at(index))->get_name();
                std::wstring message =
                    L"sequence item " + std::to_wstring(index) + L": expected ";
                message += expected_type;
                message += L", ";
                message.append(name.extract()->data,
                               std::min(size_t(name.extract()->count.extract()),
                                        size_t(80)));
                message += L" found";
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", message.c_str());
            }
            if(view->size() > limit - total)
                return thread->set_pending_builtin_exception_string(
                    L"OverflowError", overflow_message);
            total += view->size();
            if(index != 0)
            {
                if(separator_size > limit - total)
                    return thread->set_pending_builtin_exception_string(
                        L"OverflowError", overflow_message);
                total += separator_size;
            }
        }
        auto result = thread->make_object_value<Result>(
            TValue<SMI>::from_smi(static_cast<int64_t>(total)));
        // No payload pointer survives allocation. Reacquire all views here;
        // no allocations, callbacks, mutations or safepoints occur while
        // copying.
        size_t offset = 0;
        for(size_t index = 0; index < count; ++index)
        {
            if(index != 0 && separator_size != 0)
            {
                std::memcpy(result.extract()->data + offset,
                            separator.extract()->data,
                            separator_size * sizeof(Char));
                offset += separator_size;
            }
            auto view = item_view(item_at(index));
            assert(view);
            if(!view->empty())
            {
                std::memcpy(result.extract()->data + offset, view->data(),
                            view->size() * sizeof(Char));
                offset += view->size();
            }
        }
        assert(offset == total);
        return result.raw_value();
    }
}  // namespace cl

#endif  // CL_JOIN_H
