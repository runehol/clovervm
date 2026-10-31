#ifndef CL_BINARY_SPLIT_H
#define CL_BINARY_SPLIT_H

#include "builtin_types/int.h"
#include "runtime/exception_propagation.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace cl
{
    enum class BinarySplitDirection
    {
        Left,
        Right
    };

    // Constant-space Two-Way search. The critical factorization follows
    // CPython Objects/stringlib/fastsearch.h at v3.14.10 (PSF License;
    // Copyright (c) 2001 Python Software Foundation; All Rights Reserved).
    // This is the core algorithm without CPython's skip table or
    // readable-end-sentinel assumptions.
    // The separator is borrowed only within the caller's synchronous scan:
    // no VM allocation, callbacks, mutation or safepoints may occur here.
    class BinarySeparatorSearch
    {
        enum class AlphabetOrder
        {
            Normal,
            Inverted
        };

        struct Factorization
        {
            size_t cut;
            size_t period;
        };

        // Release measurements favor libc++ for ordinary short separators:
        // retain that cheap path, but cap its repeated-prefix comparison cost.
        // Longer patterns use constant-space, linear-time Two-Way search.
        static constexpr size_t short_separator_limit = 16;
        std::string_view separator_;
        BinarySplitDirection direction_;
        size_t cut_ = 0;
        size_t period_ = 0;
        size_t memory_after_shift_ = 0;

        template <BinarySplitDirection Direction>
        static uint8_t byte_at(std::string_view view, size_t index)
        {
            assert(index < view.size());
            if constexpr(Direction == BinarySplitDirection::Right)
                index = view.size() - 1 - index;
            return static_cast<uint8_t>(view[index]);
        }

        template <BinarySplitDirection Direction>
        Factorization maximal_suffix(AlphabetOrder order) const
        {
            size_t suffix = 0;
            size_t candidate = 1;
            size_t matched = 0;
            size_t period = 1;
            while(matched < separator_.size() - candidate)
            {
                uint8_t a = byte_at<Direction>(separator_, candidate + matched);
                uint8_t b = byte_at<Direction>(separator_, suffix + matched);
                if(order == AlphabetOrder::Normal ? a < b : b < a)
                {
                    candidate += matched + 1;
                    matched = 0;
                    period = candidate - suffix;
                }
                else if(a == b)
                {
                    if(matched + 1 != period)
                        ++matched;
                    else
                    {
                        candidate += period;
                        matched = 0;
                    }
                }
                else
                {
                    suffix = candidate++;
                    matched = 0;
                    period = 1;
                }
            }
            return {suffix, period};
        }

        template <BinarySplitDirection Direction> void preprocess()
        {
            auto first = maximal_suffix<Direction>(AlphabetOrder::Normal);
            auto second = maximal_suffix<Direction>(AlphabetOrder::Inverted);
            auto factor = first.cut > second.cut ? first : second;
            cut_ = factor.cut;
            period_ = factor.period;
            assert(period_ <= separator_.size() - cut_);
            for(size_t index = 0; index < cut_; ++index)
            {
                if(byte_at<Direction>(separator_, index) !=
                   byte_at<Direction>(separator_, period_ + index))
                {
                    // Non-periodic patterns cannot overlap at a smaller shift.
                    period_ = std::max(cut_, separator_.size() - cut_) + 1;
                    return;
                }
            }
            assert(cut_ <= separator_.size() / 2);
            assert(cut_ < period_);
            memory_after_shift_ = separator_.size() - period_;
        }

        template <BinarySplitDirection Direction>
        size_t mismatch_right(std::string_view range, size_t position,
                              size_t right) const
        {
            if constexpr(Direction == BinarySplitDirection::Left)
            {
                const char *first = separator_.data() + right;
                auto mismatch =
                    std::mismatch(first, separator_.data() + separator_.size(),
                                  range.data() + position + right);
                return right + static_cast<size_t>(mismatch.first - first);
            }
            else
            {
                auto first = std::make_reverse_iterator(
                    separator_.data() + separator_.size() - right);
                auto mismatch = std::mismatch(
                    first, std::make_reverse_iterator(separator_.data()),
                    std::make_reverse_iterator(range.data() + range.size() -
                                               position - right));
                return right + static_cast<size_t>(mismatch.first - first);
            }
        }

        template <BinarySplitDirection Direction>
        bool matches_left(std::string_view range, size_t position,
                          size_t memory) const
        {
            if(memory >= cut_)
                return true;
            // This phase only needs equality, not the mismatch location.
            // Physical forward slices allow optimized library comparisons in
            // both directions. Periodic cuts are at most half the pattern;
            // non-periodic failures shift by more than the left-half length.
            if constexpr(Direction == BinarySplitDirection::Left)
                return std::equal(separator_.data() + memory,
                                  separator_.data() + cut_,
                                  range.data() + position + memory);
            else
                return std::equal(
                    separator_.data() + separator_.size() - cut_,
                    separator_.data() + separator_.size() - memory,
                    range.data() + range.size() - position - cut_);
        }

        template <BinarySplitDirection Direction>
        size_t find_two_way(std::string_view range) const
        {
            const size_t size = separator_.size();
            const size_t last_start = range.size() - size;
            size_t position = 0;
            size_t memory = 0;
            const uint8_t critical_byte = byte_at<Direction>(separator_, cut_);
            while(true)
            {
                // Every match must agree at the critical cut. Character
                // search skips impossible candidates efficiently; its scans
                // advance monotonically, preserving the linear bound. A
                // periodic overlap with remembered bytes keeps its memory.
                if(memory == 0 &&
                   byte_at<Direction>(range, position + cut_) != critical_byte)
                {
                    size_t candidate;
                    if constexpr(Direction == BinarySplitDirection::Left)
                    {
                        candidate = range.find(static_cast<char>(critical_byte),
                                               position + cut_);
                        if(candidate == std::string_view::npos)
                            return candidate;
                        candidate -= cut_;
                    }
                    else
                    {
                        candidate =
                            range.rfind(static_cast<char>(critical_byte),
                                        range.size() - 1 - position - cut_);
                        if(candidate == std::string_view::npos)
                            return candidate;
                        candidate = range.size() - 1 - candidate - cut_;
                    }
                    if(candidate > last_start)
                        return std::string_view::npos;
                    position = candidate;
                }
                size_t right = mismatch_right<Direction>(
                    range, position, std::max(cut_, memory));
                size_t shift;
                if(right < size)
                {
                    shift = right - cut_ + 1;
                    memory = 0;
                }
                else
                {
                    if(matches_left<Direction>(range, position, memory))
                    {
                        if constexpr(Direction == BinarySplitDirection::Left)
                            return position;
                        else
                            return range.size() - position - size;
                    }
                    shift = period_;
                    memory = memory_after_shift_;
                }
                // Check before adding: no overflow or out-of-range lookahead.
                if(shift > last_start - position)
                    return std::string_view::npos;
                position += shift;
            }
        }

    public:
        BinarySeparatorSearch(std::string_view separator,
                              BinarySplitDirection direction)
            : separator_(separator), direction_(direction)
        {
            assert(!separator.empty());
            if(separator.size() <= short_separator_limit)
                return;
            if(direction == BinarySplitDirection::Left)
                preprocess<BinarySplitDirection::Left>();
            else
                preprocess<BinarySplitDirection::Right>();
        }

        // Returns an absolute source offset. Callers validate nonempty
        // separators and retain their owners for this entire scan.
        size_t find(std::string_view source, size_t start, size_t end) const
        {
            assert(start <= end && end <= source.size());
            if(separator_.size() > end - start)
                return std::string_view::npos;
            auto range = source.substr(start, end - start);
            size_t found;
            if(separator_.size() <= short_separator_limit)
                found = direction_ == BinarySplitDirection::Left
                            ? range.find(separator_)
                            : range.rfind(separator_);
            else if(direction_ == BinarySplitDirection::Left)
                found = find_two_way<BinarySplitDirection::Left>(range);
            else
                found = find_two_way<BinarySplitDirection::Right>(range);
            return found == std::string_view::npos ? found : start + found;
        }
    };

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
                      int64_t maxsplit, BinarySplitDirection direction)
    {
        std::vector<BinarySplitPiece> pieces;
        {
            size_t start = 0;
            size_t end = view.size();
            if(!separator.has_value())
            {
                while(start < end)
                {
                    if(direction == BinarySplitDirection::Left)
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
                    if(direction == BinarySplitDirection::Left)
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
                    BinarySeparatorSearch search(sep, direction);
                    while(maxsplit != 0)
                    {
                        size_t found = search.find(view, start, end);
                        if(found == std::string_view::npos)
                            break;
                        if(direction == BinarySplitDirection::Left)
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
        if(direction == BinarySplitDirection::Right)
            std::reverse(pieces.begin(), pieces.end());
        return pieces;
    }
}  // namespace cl

#endif  // CL_BINARY_SPLIT_H
