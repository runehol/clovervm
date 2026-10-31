#ifndef CL_BINARY_SPLIT_H
#define CL_BINARY_SPLIT_H

#include "builtin_types/int.h"
#include "builtin_types/separator_search.h"
#include "runtime/exception_propagation.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace cl
{
    struct BinarySplitPiece
    {
        size_t start;
        size_t size;
    };

    [[nodiscard]] inline Expected<int64_t> binary_maxsplit(Value value)
    {
        if(!is_intlike_value(value))
        {
            return Expected<int64_t>::raise_exception(
                L"TypeError", L"maxsplit must be an integer");
        }
        SmiViewStorage storage;
        int64_t count =
            CL_TRY(bigint_to_int64(intlike_value_bigint_view(value, &storage)));
        if(count < std::numeric_limits<ptrdiff_t>::min() ||
           count > std::numeric_limits<ptrdiff_t>::max())
        {
            return Expected<int64_t>::raise_exception(
                L"OverflowError", L"maxsplit exceeds platform index range");
        }
        return Expected<int64_t>::ok(count);
    }

    inline bool is_binary_split_space(char ch)
    {
        return ch == ' ' || (ch >= '\t' && ch <= '\r');
    }

    // Synchronous scan only: no VM allocation, callbacks, mutation or
    // safepoints. Callers validate nonempty explicit separators. Only offsets
    // survive the scan, never pointers into either stored payload.
    inline std::vector<BinarySplitPiece>
    scan_binary_split(std::string_view view,
                      std::optional<std::string_view> separator,
                      int64_t maxsplit, SearchDirection direction)
    {
        std::vector<BinarySplitPiece> pieces;
        {
            size_t start = 0;
            size_t end = view.size();
            if(!separator.has_value())
            {
                while(start < end)
                {
                    if(direction == SearchDirection::Forward)
                    {
                        while(start < end && is_binary_split_space(view[start]))
                            ++start;
                    }
                    else
                    {
                        while(end > start &&
                              is_binary_split_space(view[end - 1]))
                            --end;
                    }
                    if(start == end)
                        break;
                    if(maxsplit == 0)
                    {
                        pieces.push_back({start, end - start});
                        break;
                    }
                    if(direction == SearchDirection::Forward)
                    {
                        size_t stop = start;
                        while(stop < end && !is_binary_split_space(view[stop]))
                            ++stop;
                        pieces.push_back({start, stop - start});
                        start = stop;
                    }
                    else
                    {
                        size_t stop = end;
                        while(stop > start &&
                              !is_binary_split_space(view[stop - 1]))
                            --stop;
                        pieces.push_back({stop, end - stop});
                        end = stop;
                    }
                    if(maxsplit > 0)
                        --maxsplit;
                }
            }
            else
            {
                auto sep = *separator;
                assert(!sep.empty());
                if(maxsplit != 0 && sep.size() <= view.size())
                {
                    SeparatorSearch<char> search(sep, direction);
                    while(maxsplit != 0)
                    {
                        size_t found = search.find(view, start, end);
                        if(found == std::string_view::npos)
                            break;
                        if(direction == SearchDirection::Forward)
                        {
                            pieces.push_back({start, found - start});
                            start = found + sep.size();
                        }
                        else
                        {
                            pieces.push_back(
                                {found + sep.size(), end - found - sep.size()});
                            end = found;
                        }
                        if(maxsplit > 0)
                            --maxsplit;
                    }
                }
                pieces.push_back({start, end - start});
            }
        }
        if(direction == SearchDirection::Reverse)
            std::reverse(pieces.begin(), pieces.end());
        return pieces;
    }
}  // namespace cl

#endif  // CL_BINARY_SPLIT_H
