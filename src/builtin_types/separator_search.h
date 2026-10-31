#ifndef CL_SEPARATOR_SEARCH_H
#define CL_SEPARATOR_SEARCH_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <type_traits>

namespace cl
{
    enum class SearchDirection
    {
        Forward,
        Reverse
    };

    // Constant-space Two-Way search. The critical factorization follows
    // CPython Objects/stringlib/fastsearch.h at v3.14.10 (PSF License;
    // Copyright (c) 2001 Python Software Foundation; All Rights Reserved).
    // This is the core algorithm without CPython's skip table or
    // readable-end-sentinel assumptions.
    // The separator is borrowed only within the caller's synchronous scan:
    // no VM allocation, callbacks, mutation or safepoints may occur here.
    template <typename CharT> class SeparatorSearch
    {
        using View = std::basic_string_view<CharT>;
        using UnsignedChar = std::make_unsigned_t<CharT>;

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

        // Release measurements retain library search through 16 bytes or eight
        // wide characters. Bounded lengths cap repeated-prefix comparison cost.
        static constexpr size_t short_separator_limit =
            sizeof(CharT) == 1 ? 16 : 8;
        View separator_;
        SearchDirection direction_;
        size_t cut_ = 0;
        size_t period_ = 0;
        size_t memory_after_shift_ = 0;

        template <SearchDirection Direction>
        static UnsignedChar char_at(View view, size_t index)
        {
            assert(index < view.size());
            if constexpr(Direction == SearchDirection::Reverse)
                index = view.size() - 1 - index;
            return static_cast<UnsignedChar>(view[index]);
        }

        template <SearchDirection Direction>
        Factorization maximal_suffix(AlphabetOrder order) const
        {
            size_t suffix = 0;
            size_t candidate = 1;
            size_t matched = 0;
            size_t period = 1;
            while(matched < separator_.size() - candidate)
            {
                UnsignedChar a =
                    char_at<Direction>(separator_, candidate + matched);
                UnsignedChar b =
                    char_at<Direction>(separator_, suffix + matched);
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

        template <SearchDirection Direction> void preprocess()
        {
            auto first = maximal_suffix<Direction>(AlphabetOrder::Normal);
            auto second = maximal_suffix<Direction>(AlphabetOrder::Inverted);
            auto factor = first.cut > second.cut ? first : second;
            cut_ = factor.cut;
            period_ = factor.period;
            assert(period_ <= separator_.size() - cut_);
            for(size_t index = 0; index < cut_; ++index)
            {
                if(char_at<Direction>(separator_, index) !=
                   char_at<Direction>(separator_, period_ + index))
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

        template <SearchDirection Direction>
        size_t mismatch_right(View range, size_t position, size_t right) const
        {
            if constexpr(Direction == SearchDirection::Forward)
            {
                const CharT *first = separator_.data() + right;
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

        template <SearchDirection Direction>
        bool matches_left(View range, size_t position, size_t memory) const
        {
            if(memory >= cut_)
                return true;
            // This phase only needs equality, not the mismatch location.
            // Physical forward slices allow optimized library comparisons in
            // both directions. Periodic cuts are at most half the pattern;
            // non-periodic failures shift by more than the left-half length.
            if constexpr(Direction == SearchDirection::Forward)
                return std::equal(separator_.data() + memory,
                                  separator_.data() + cut_,
                                  range.data() + position + memory);
            else
                return std::equal(
                    separator_.data() + separator_.size() - cut_,
                    separator_.data() + separator_.size() - memory,
                    range.data() + range.size() - position - cut_);
        }

        template <SearchDirection Direction>
        size_t find_two_way(View range) const
        {
            const size_t size = separator_.size();
            const size_t last_start = range.size() - size;
            size_t position = 0;
            size_t memory = 0;
            const UnsignedChar critical_char =
                char_at<Direction>(separator_, cut_);
            while(true)
            {
                // Every match must agree at the critical cut. Character
                // search skips impossible candidates efficiently; its scans
                // advance monotonically, preserving the linear bound. A
                // periodic overlap with remembered bytes keeps its memory.
                if(memory == 0 &&
                   char_at<Direction>(range, position + cut_) != critical_char)
                {
                    size_t candidate;
                    if constexpr(Direction == SearchDirection::Forward)
                    {
                        candidate = range.find(
                            static_cast<CharT>(critical_char), position + cut_);
                        if(candidate == View::npos)
                            return candidate;
                        candidate -= cut_;
                    }
                    else
                    {
                        candidate =
                            range.rfind(static_cast<CharT>(critical_char),
                                        range.size() - 1 - position - cut_);
                        if(candidate == View::npos)
                            return candidate;
                        candidate = range.size() - 1 - candidate - cut_;
                    }
                    if(candidate > last_start)
                        return View::npos;
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
                        if constexpr(Direction == SearchDirection::Forward)
                            return position;
                        else
                            return range.size() - position - size;
                    }
                    shift = period_;
                    memory = memory_after_shift_;
                }
                // Check before adding: no overflow or out-of-range lookahead.
                if(shift > last_start - position)
                    return View::npos;
                position += shift;
            }
        }

    public:
        SeparatorSearch(View separator, SearchDirection direction)
            : separator_(separator), direction_(direction)
        {
            assert(!separator.empty());
            if(separator.size() <= short_separator_limit)
                return;
            if(direction == SearchDirection::Forward)
                preprocess<SearchDirection::Forward>();
            else
                preprocess<SearchDirection::Reverse>();
        }

        // Returns an absolute source offset. Callers validate nonempty
        // separators and retain their owners for this entire scan.
        size_t find(View source, size_t start, size_t end) const
        {
            assert(start <= end && end <= source.size());
            if(separator_.size() > end - start)
                return View::npos;
            auto range = source.substr(start, end - start);
            size_t found;
            if(separator_.size() <= short_separator_limit)
                found = direction_ == SearchDirection::Forward
                            ? range.find(separator_)
                            : range.rfind(separator_);
            else if(direction_ == SearchDirection::Forward)
                found = find_two_way<SearchDirection::Forward>(range);
            else
                found = find_two_way<SearchDirection::Reverse>(range);
            return found == View::npos ? found : start + found;
        }
    };

}  // namespace cl

#endif  // CL_SEPARATOR_SEARCH_H
