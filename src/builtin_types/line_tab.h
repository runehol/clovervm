#ifndef CL_LINE_TAB_H
#define CL_LINE_TAB_H

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
    enum class LineBoundaryKind
    {
        Unicode,
        Binary
    };
    enum class KeepLineEnds
    {
        Discard,
        Keep
    };
    struct LinePiece
    {
        size_t start;
        size_t size;
        size_t next;
    };

    // Unchecked synchronous payload scans: no VM allocations, Python calls,
    // mutations or safepoints. Only offsets may survive returning to the
    // caller.
    template <LineBoundaryKind Kind, typename Char>
    [[nodiscard]] std::optional<LinePiece>
    next_line(std::span<const Char> source, size_t start, KeepLineEnds keep)
    {
        assert(start <= source.size());
        if(start == source.size())
            return std::nullopt;
        size_t end = start;
        for(; end < source.size(); ++end)
        {
            auto ch = source[end];
            if(ch == '\r' || ch == '\n')
                break;
            if constexpr(Kind == LineBoundaryKind::Unicode)
                if(ch == 0x0b || ch == 0x0c || ch == 0x1c || ch == 0x1d ||
                   ch == 0x1e || ch == 0x85 || ch == 0x2028 || ch == 0x2029)
                    break;
        }
        size_t next = end;
        if(next < source.size())
        {
            if(source[next++] == '\r' && next < source.size() &&
               source[next] == '\n')
                ++next;
        }
        return LinePiece{
            start, (keep == KeepLineEnds::Keep ? next : end) - start, next};
    }

    enum class TabPass
    {
        Measure,
        Write
    };
    struct TabExpansion
    {
        size_t size;
        bool has_tabs;
    };

    // The same loop performs sizing and filling. Write callers must provide
    // the successfully measured capacity and the same source/tabsize/limit.
    // Overflow is ordinary native status, not pending-exception state.
    template <TabPass Pass, typename Char>
    [[nodiscard]] std::optional<TabExpansion>
    expand_tabs(std::span<const Char> source, int32_t tabsize, size_t limit,
                std::span<Char> output = {})
    {
        size_t total = 0, column = 0;
        bool has_tabs = false;
        for(Char ch: source)
        {
            size_t count = 1;
            if(ch == '\t')
            {
                has_tabs = true;
                count = tabsize > 0 ? size_t(tabsize) - column % size_t(tabsize)
                                    : 0;
            }
            if(count > limit - total)
                return std::nullopt;
            if constexpr(Pass == TabPass::Write)
            {
                assert(total <= output.size() &&
                       count <= output.size() - total);
                if(ch == '\t')
                {
                    if(count != 0)
                        std::fill_n(output.data() + total, count, Char(' '));
                }
                else
                    output[total] = ch;
            }
            total += count;
            column += count;
            if(ch == '\r' || ch == '\n')
                column = 0;
        }
        return TabExpansion{total, has_tabs};
    }

    // Public argument policy, separate from the unchecked payload algorithms.
    // Accept stored integer payloads only; no general __index__ dispatch.
    [[nodiscard]] inline Expected<int32_t> stored_tabsize(Value value)
    {
        if(!is_intlike_value(value))
            return Expected<int32_t>::raise_exception(
                L"TypeError", L"tabsize must be an integer");
        SmiViewStorage storage;
        int64_t size =
            CL_TRY(bigint_to_int64(intlike_value_bigint_view(value, &storage)));
        if(size < std::numeric_limits<int32_t>::min() ||
           size > std::numeric_limits<int32_t>::max())
            return Expected<int32_t>::raise_exception(
                L"OverflowError", L"tabsize exceeds C int range");
        return Expected<int32_t>::ok(static_cast<int32_t>(size));
    }
}  // namespace cl
#endif
