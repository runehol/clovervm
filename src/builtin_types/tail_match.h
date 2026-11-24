#ifndef CL_TAIL_MATCH_H
#define CL_TAIL_MATCH_H

#include "builtin_types/tuple.h"
#include "object_model/typed_value.h"
#include "runtime/thread_state.h"
#include <cstddef>

namespace cl
{
    enum class TailMatch
    {
        Prefix,
        Suffix
    };

    template <TailMatch Tail, typename T>
    [[nodiscard]] Value match_tail_candidates(ThreadState *thread,
                                              TValue<T> source, Value candidate,
                                              size_t start, size_t end,
                                              const wchar_t *argument_error,
                                              const wchar_t *item_error)
    {
        auto matches = [&](TValue<T> item) {
            if constexpr(Tail == TailMatch::Prefix)
                return source.extract()->startswith(item.extract(), start, end);
            else
                return source.extract()->endswith(item.extract(), start, end);
        };

        if(has_native_layout<T>(candidate))
            return matches(TValue<T>::from_value_assumed(candidate))
                       ? Value::True()
                       : Value::False();
        if(!has_native_layout<Tuple>(candidate))
            return thread->set_pending_builtin_exception_string(L"TypeError",
                                                                argument_error);

        // The call arguments keep the source and tuple alive. Read stored
        // elements without callbacks and validate each only when reached.
        const auto *tuple = candidate.get_ptr<Tuple>();
        for(size_t i = 0; i < tuple->size(); ++i)
        {
            Value item = tuple->item_unchecked(i);
            if(!has_native_layout<T>(item))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", item_error);
            if(matches(TValue<T>::from_value_assumed(item)))
                return Value::True();
        }
        return Value::False();
    }
}  // namespace cl

#endif  // CL_TAIL_MATCH_H
