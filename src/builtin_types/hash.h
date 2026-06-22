#ifndef CL_HASH_H
#define CL_HASH_H

#include "object_model/typed_value.h"
#include "object_model/value.h"

#include <cstdint>

namespace cl
{
    // BigInt hash reduction relies on this modulus having Mersenne form
    // 2**n - 1, so high bits can be folded back into the low bits without a
    // wider-than-64-bit intermediate.
    static constexpr uint32_t clover_hash_modulus_bits =
        64 - value_tag_bits - 1;
    static constexpr int64_t clover_hash_modulus =
        (int64_t{1} << clover_hash_modulus_bits) - 1;
    static_assert(clover_hash_modulus <= value_smi_max);

    [[nodiscard]] Expected<TValue<SMI>> canonicalize_hash_result(Value value);
    TValue<SMI> canonicalize_nonnegative_raw_hash(uint64_t hash);

}  // namespace cl

#endif  // CL_HASH_H
