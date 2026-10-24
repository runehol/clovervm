#ifndef CL_SEARCH_RANGE_H
#define CL_SEARCH_RANGE_H

#include "builtin_types/int.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "util/result.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cl
{
    inline size_t normalize_search_bound(int64_t value, int64_t length)
    {
        if(value < 0)
        {
            value += length;
        }
        if(value < 0)
        {
            return 0;
        }
        return static_cast<size_t>(value);
    }

    // Preserve starts beyond the end and reversed ranges as no-match ranges.
    // Only the end is capped at the sequence length.
    [[nodiscard]] inline Value
    normalize_search_range(int64_t length, Value start_value, Value end_value,
                           size_t &start, size_t &end)
    {
        int64_t py_start = 0;
        int64_t py_end = length;
        if(!start_value.is_none())
        {
            TValue<SMI> index = CL_TRY(try_intlike_value_to_smi(
                start_value, L"slice indices must be integers"));
            py_start = index.extract();
        }
        if(!end_value.is_none())
        {
            TValue<SMI> index = CL_TRY(try_intlike_value_to_smi(
                end_value, L"slice indices must be integers"));
            py_end = index.extract();
        }
        start = normalize_search_bound(py_start, length);
        end = std::min(normalize_search_bound(py_end, length),
                       static_cast<size_t>(length));
        return Value::None();
    }
}  // namespace cl

#endif  // CL_SEARCH_RANGE_H
