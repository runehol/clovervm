#include "builtin_types/separator_search.h"
#include "test_helpers.h"
#include <random>
#include <string>

using namespace cl;

namespace
{
    size_t naive_binary_match(std::string_view source,
                              std::string_view separator, size_t start,
                              size_t end, SearchDirection direction)
    {
        if(separator.size() > end - start)
            return std::string_view::npos;
        size_t last = end - separator.size();
        for(size_t offset = 0; offset <= last - start; ++offset)
        {
            size_t position = direction == SearchDirection::Forward
                                  ? start + offset
                                  : last - offset;
            size_t index = 0;
            while(index < separator.size() &&
                  source[position + index] == separator[index])
                ++index;
            if(index == separator.size())
                return position;
        }
        return std::string_view::npos;
    }

    std::string binary_word(size_t mask, size_t length, size_t stride)
    {
        std::string result(length * stride, '\0');
        for(size_t index = 0; index < length; ++index)
            for(size_t repeat = 0; repeat < stride; ++repeat)
                result[index * stride + repeat] =
                    (mask & (size_t(1) << index)) ? char(0xff) : '\0';
        return result;
    }
}  // namespace

TEST(BinarySeparatorSearch, ExhaustiveBinaryWordsAndClippedRanges)
{
    // Striding the same exhaustive words exercises both the short path and
    // Two-Way, with range boundaries also falling inside repeated runs.
    for(size_t stride: {size_t(1), size_t(4)})
        for(size_t source_length = 0; source_length <= 8; ++source_length)
            for(size_t source_mask = 0;
                source_mask < (size_t(1) << source_length); ++source_mask)
            {
                auto source = binary_word(source_mask, source_length, stride);
                for(size_t separator_length = 1; separator_length <= 6;
                    ++separator_length)
                    for(size_t separator_mask = 0;
                        separator_mask < (size_t(1) << separator_length);
                        ++separator_mask)
                    {
                        auto separator = binary_word(separator_mask,
                                                     separator_length, stride);
                        for(auto direction: {SearchDirection::Forward,
                                             SearchDirection::Reverse})
                        {
                            SeparatorSearch<char> search(separator, direction);
                            for(size_t boundary = 0; boundary <= source.size();
                                ++boundary)
                            {
                                ASSERT_EQ(naive_binary_match(source, separator,
                                                             0, boundary,
                                                             direction),
                                          search.find(source, 0, boundary));
                                ASSERT_EQ(naive_binary_match(
                                              source, separator, boundary,
                                              source.size(), direction),
                                          search.find(source, boundary,
                                                      source.size()));
                            }
                        }
                    }
            }
}

TEST(BinarySeparatorSearch, RandomPeriodicPatternsAndRepeatedRangeSearch)
{
    std::mt19937 random(86421);
    for(size_t trial = 0; trial < 20000; ++trial)
    {
        SCOPED_TRACE(trial);
        std::string source(random() % 385, '\0');
        for(char &byte: source)
            byte = static_cast<char>(trial % 2 ? random() % 4 : random() % 256);
        size_t period = 1 + random() % 13;
        if(trial % 3 == 0)
            for(size_t index = period; index < source.size(); ++index)
                source[index] = source[index % period];
        std::string separator(1 + random() % 129, '\0');
        for(char &byte: separator)
            byte = static_cast<char>(random() % 256);
        if(separator.size() <= source.size() && trial % 2)
        {
            size_t start = random() % (source.size() - separator.size() + 1);
            separator = source.substr(start, separator.size());
            if(trial % 5 == 0)
                separator[random() % separator.size()] ^= char(0x80);
        }
        for(auto direction:
            {SearchDirection::Forward, SearchDirection::Reverse})
        {
            SeparatorSearch<char> search(separator, direction);
            for(size_t repeat = 0; repeat < 4; ++repeat)
            {
                size_t start = repeat == 0 ? 0 : random() % (source.size() + 1);
                size_t end =
                    repeat == 0
                        ? source.size()
                        : start + random() % (source.size() - start + 1);
                ASSERT_EQ(naive_binary_match(source, separator, start, end,
                                             direction),
                          search.find(source, start, end));
            }
        }
    }
}

TEST(BinarySeparatorSearch, LongForwardAndReverseNearMisses)
{
    std::string source(65536, 'a');
    for(auto direction: {SearchDirection::Forward, SearchDirection::Reverse})
    {
        std::string separator(32768, 'a');
        if(direction == SearchDirection::Forward)
            separator.back() = 'b';
        else
            separator.front() = 'b';
        SeparatorSearch<char> search(separator, direction);
        EXPECT_EQ(std::string_view::npos,
                  search.find(source, 0, source.size()));
    }
}

namespace
{
    size_t naive_wide_match(std::wstring_view source,
                            std::wstring_view separator, size_t start,
                            size_t end, SearchDirection direction)
    {
        if(separator.size() > end - start)
            return std::wstring_view::npos;
        const size_t last = end - separator.size();
        for(size_t offset = 0; offset <= last - start; ++offset)
        {
            size_t pos = direction == SearchDirection::Forward ? start + offset
                                                               : last - offset;
            size_t index = 0;
            while(index < separator.size() &&
                  source[pos + index] == separator[index])
                ++index;
            if(index == separator.size())
                return pos;
        }
        return std::wstring_view::npos;
    }
}  // namespace

TEST(WideSeparatorSearch, FullWidthAlphabetPeriodicAndClippedRanges)
{
    std::mt19937 random(342786);
    // Values with identical low bytes detect accidental narrowing to uint8_t.
    constexpr wchar_t alphabet[] = {0,      0x100,  0x200,   0x180,   0x280,
                                    0xd800, 0xffff, 0x10000, 0x10ffff};
    for(size_t trial = 0; trial < 30000; ++trial)
    {
        SCOPED_TRACE(trial);
        std::wstring source(random() % 513, L'\0');
        for(auto &ch: source)
            ch = alphabet[random() % std::size(alphabet)];
        size_t period = 1 + random() % 19;
        if(trial % 3 == 0)
            for(size_t index = period; index < source.size(); ++index)
                source[index] = source[index % period];
        std::wstring separator(1 + random() % 129, L'\0');
        for(auto &ch: separator)
            ch = alphabet[random() % std::size(alphabet)];
        if(separator.size() <= source.size() && trial % 2)
        {
            size_t offset = random() % (source.size() - separator.size() + 1);
            separator = source.substr(offset, separator.size());
            if(trial % 5 == 0)
                separator[random() % separator.size()] ^= wchar_t(0x100);
        }
        for(auto direction:
            {SearchDirection::Forward, SearchDirection::Reverse})
        {
            SeparatorSearch<wchar_t> search(separator, direction);
            for(size_t repeat = 0; repeat < 5; ++repeat)
            {
                size_t start = repeat == 0 ? 0 : random() % (source.size() + 1);
                size_t end =
                    repeat == 0
                        ? source.size()
                        : start + random() % (source.size() - start + 1);
                ASSERT_EQ(
                    naive_wide_match(source, separator, start, end, direction),
                    search.find(source, start, end));
            }
            size_t start = 0, end = source.size();
            while(true)
            {
                size_t found = search.find(source, start, end);
                ASSERT_EQ(
                    naive_wide_match(source, separator, start, end, direction),
                    found);
                if(found == std::wstring_view::npos)
                    break;
                if(direction == SearchDirection::Forward)
                    start = found + separator.size();
                else
                    end = found;
            }
        }
    }
}

TEST(WideSeparatorSearch, ExhaustiveExpandedWords)
{
    for(size_t mask = 0; mask < 256; ++mask)
    {
        auto narrow = binary_word(mask, 8, 3);
        std::wstring source;
        for(auto ch: narrow)
            source.push_back(ch ? wchar_t(0x10000) : wchar_t(0x100));
        for(size_t width = 1; width <= 5; ++width)
            for(size_t sep_mask = 0; sep_mask < (size_t(1) << width);
                ++sep_mask)
            {
                auto narrow_sep = binary_word(sep_mask, width, 3);
                std::wstring separator;
                for(auto ch: narrow_sep)
                    separator.push_back(ch ? wchar_t(0x10000) : wchar_t(0x100));
                for(auto direction:
                    {SearchDirection::Forward, SearchDirection::Reverse})
                {
                    SeparatorSearch<wchar_t> search(separator, direction);
                    for(size_t boundary = 0; boundary <= source.size();
                        ++boundary)
                    {
                        ASSERT_EQ(naive_wide_match(source, separator, 0,
                                                   boundary, direction),
                                  search.find(source, 0, boundary));
                        ASSERT_EQ(naive_wide_match(source, separator, boundary,
                                                   source.size(), direction),
                                  search.find(source, boundary, source.size()));
                    }
                }
            }
    }
}
