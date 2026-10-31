#ifndef CL_PADDING_H
#define CL_PADDING_H

#include "builtin_types/int.h"
#include "runtime/exception_propagation.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace cl
{
    enum class PaddingKind
    {
        Left,
        Right,
        Center,
        Zero
    };

    struct PaddingLayout
    {
        size_t size;
        size_t left;
    };

    // Scalar sizing only: the caller has validated width and supplies the
    // owning type's storage limit. No pending-exception state is involved.
    [[nodiscard]] inline std::optional<PaddingLayout>
    plan_padding(size_t length, int64_t width, size_t limit, PaddingKind kind)
    {
        assert(length <= limit);
        if(width <= 0 || static_cast<uint64_t>(width) <= length)
            return PaddingLayout{length, 0};
        if(static_cast<uint64_t>(width) > limit)
            return std::nullopt;
        size_t size = static_cast<size_t>(width);
        size_t margin = size - length;
        size_t left = 0;
        if(kind == PaddingKind::Right || kind == PaddingKind::Zero)
            left = margin;
        else if(kind == PaddingKind::Center)
            left = margin / 2 + (margin & size & 1);
        return PaddingLayout{size, left};
    }

    // Unchecked synchronous payload write. Callers supply disjoint source and
    // output, and the layout measured for this source. No allocations, Python
    // callbacks, mutation through aliases or safepoints may occur while
    // borrowed.
    template <typename Char>
    void write_padding(std::span<const Char> source, Char fill,
                       PaddingLayout plan, std::span<Char> output,
                       PaddingKind kind)
    {
        assert(output.size() == plan.size);
        assert(plan.left <= plan.size &&
               source.size() <= plan.size - plan.left);
        // An empty result may have null backing storage.
        if(output.empty())
            return;
        std::fill_n(output.data(), plan.left, fill);
        if(!source.empty())
            std::copy_n(source.data(), source.size(),
                        output.data() + plan.left);
        std::fill_n(output.data() + plan.left + source.size(),
                    plan.size - plan.left - source.size(), fill);
        if(kind == PaddingKind::Zero)
        {
            if(plan.left != 0 && !source.empty())
            {
                Char first = output[plan.left];
                if(first == Char('+') || first == Char('-'))
                {
                    output[0] = first;
                    output[plan.left] = Char('0');
                }
            }
        }
    }

    // Public argument policy, separate from the unchecked payload algorithms.
    // Stored integers include bools and native int subclasses; custom __index__
    // dispatch remains outside this bounded API.
    [[nodiscard]] inline Expected<int64_t> stored_padding_width(Value value)
    {
        if(!is_intlike_value(value))
            return Expected<int64_t>::raise_exception(
                L"TypeError", L"width must be an integer");
        SmiViewStorage storage;
        int64_t width =
            CL_TRY(bigint_to_int64(intlike_value_bigint_view(value, &storage)));
        if(width < std::numeric_limits<ptrdiff_t>::min() ||
           width > std::numeric_limits<ptrdiff_t>::max())
            return Expected<int64_t>::raise_exception(
                L"OverflowError", L"width exceeds platform index range");
        return Expected<int64_t>::ok(width);
    }
}  // namespace cl

#endif  // CL_PADDING_H
