#ifndef CL_HASH_H
#define CL_HASH_H

#include "object_model/typed_value.h"
#include "object_model/value.h"

#include <cstdint>

namespace cl
{
    static constexpr int64_t clover_hash_modulus = value_smi_max;

    [[nodiscard]] Expected<TValue<SMI>> canonicalize_hash_result(Value value);
    TValue<SMI> canonicalize_nonnegative_raw_hash(uint64_t hash);

}  // namespace cl

#endif  // CL_HASH_H
