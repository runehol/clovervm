#include "cpp_benchmarks.h"

#include <array>
#include <string>
#include <vector>

namespace benchmark_cpp
{
    int64_t getslice_list_nonstrided_run(int64_t n)
    {
        const std::vector<int64_t> values = {3, 5, 7, 11, 13, 17, 19, 23};
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            std::vector<int64_t> slice(values.begin() + 1, values.begin() + 5);
            acc += slice[0] + static_cast<int64_t>(slice.size());
        }
        return acc;
    }

    int64_t getslice_list_nonstrided_items(int64_t n) { return n; }

    int64_t getslice_list_general_run(int64_t n)
    {
        const std::vector<int64_t> values = {3, 5, 7, 11, 13, 17, 19, 23};
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            std::vector<int64_t> slice;
            for(int64_t idx = 7; idx > 0; idx -= 2)
            {
                slice.push_back(values[static_cast<size_t>(idx)]);
            }
            acc += slice[0] + static_cast<int64_t>(slice.size());
        }
        return acc;
    }

    int64_t getslice_list_general_items(int64_t n) { return n; }

    int64_t getslice_tuple_nonstrided_run(int64_t n)
    {
        const std::array<int64_t, 8> values = {3, 5, 7, 11, 13, 17, 19, 23};
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            std::array<int64_t, 4> slice = {values[1], values[2], values[3],
                                            values[4]};
            acc += slice[0] + static_cast<int64_t>(slice.size());
        }
        return acc;
    }

    int64_t getslice_tuple_nonstrided_items(int64_t n) { return n; }

    int64_t getslice_tuple_general_run(int64_t n)
    {
        const std::array<int64_t, 8> values = {3, 5, 7, 11, 13, 17, 19, 23};
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            std::array<int64_t, 4> slice = {values[7], values[5], values[3],
                                            values[1]};
            acc += slice[0] + static_cast<int64_t>(slice.size());
        }
        return acc;
    }

    int64_t getslice_tuple_general_items(int64_t n) { return n; }

    int64_t getslice_str_nonstrided_run(int64_t n)
    {
        const std::wstring values = L"abcdefgh";
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            std::wstring slice = values.substr(1, 4);
            acc += static_cast<int64_t>(slice.size());
        }
        return acc;
    }

    int64_t getslice_str_nonstrided_items(int64_t n) { return n; }

    int64_t getslice_str_general_run(int64_t n)
    {
        const std::wstring values = L"abcdefgh";
        int64_t acc = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            std::wstring slice;
            for(int64_t idx = 7; idx > 0; idx -= 2)
            {
                slice.push_back(values[static_cast<size_t>(idx)]);
            }
            acc += static_cast<int64_t>(slice.size());
        }
        return acc;
    }

    int64_t getslice_str_general_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
