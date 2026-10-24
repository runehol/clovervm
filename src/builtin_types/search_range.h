#ifndef CL_SEARCH_RANGE_H
#define CL_SEARCH_RANGE_H

#include "builtin_types/int.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "util/result.h"
#include <algorithm>
#include <cassert>
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
        // A start past the end must remain distinguishable from length for
        // empty needles, but its exact distance past the end is immaterial.
        return static_cast<size_t>(std::min(value, length + 1));
    }

    [[nodiscard]] inline Expected<size_t> normalize_search_index(Value value,
                                                                 int64_t length)
    {
        if(has_native_layout<BigInt>(value))
        {
            ConstBigIntView index = value.get_ptr<BigInt>()->view();
            SmiViewStorage length_storage;
            ConstBigIntView length_view =
                smi_bigint_view(length, &length_storage);
            // Compare before narrowing: even arbitrary-size bounds saturate
            // without integer-conversion overflow or Python callbacks.
            if(compare_bigint_abs(index, length_view) > 0)
            {
                return Expected<size_t>::ok(
                    index.signum < 0 ? 0 : static_cast<size_t>(length) + 1);
            }
            return Expected<size_t>::ok(
                normalize_search_bound(CL_TRY(bigint_to_int64(index)), length));
        }
        TValue<SMI> index = CL_TRY(
            try_intlike_value_to_smi(value, L"slice indices must be integers"));
        return Expected<size_t>::ok(
            normalize_search_bound(index.extract(), length));
    }

    // Preserve starts beyond the end and reversed ranges as no-match ranges.
    // Only the end is capped at the sequence length.
    [[nodiscard]] inline Value
    normalize_search_range(int64_t length, Value start_value, Value end_value,
                           size_t &start, size_t &end)
    {
        // str and bytes both store their lengths as SMIs. This also leaves
        // room for the length + 1 sentinel used for starts past the end.
        assert(length >= 0 && length <= value_smi_max);
        start = 0;
        end = static_cast<size_t>(length);
        if(!start_value.is_none())
        {
            start = CL_TRY(normalize_search_index(start_value, length));
        }
        if(!end_value.is_none())
        {
            end = CL_TRY(normalize_search_index(end_value, length));
        }
        end = std::min(end, static_cast<size_t>(length));
        return Value::None();
    }
}  // namespace cl

#endif  // CL_SEARCH_RANGE_H
