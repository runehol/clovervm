#ifndef CL_STRIP_SCAN_H
#define CL_STRIP_SCAN_H

#include <cstddef>
#include <string_view>

namespace cl
{
    enum class StripMode
    {
        Both,
        Left,
        Right
    };

    struct StripRange
    {
        size_t start;
        size_t size;
    };

    // Synchronous borrowed scan: the predicate must not allocate, call Python,
    // mutate either payload, poll a safepoint, or retain any borrowed pointer.
    // Only scalar offsets escape, before the caller constructs the result.
    template <typename CharT, typename Predicate>
    StripRange scan_strip(std::basic_string_view<CharT> view, StripMode mode,
                          Predicate removable)
    {
        size_t start = 0;
        size_t end = view.size();
        if(mode != StripMode::Right)
        {
            while(start < end && removable(view[start]))
                ++start;
        }
        if(mode != StripMode::Left)
        {
            while(end > start && removable(view[end - 1]))
                --end;
        }
        return {start, end - start};
    }
}  // namespace cl

#endif  // CL_STRIP_SCAN_H
