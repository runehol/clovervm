#include "builtin_types/hash.h"

#include "builtin_types/bigint.h"

namespace cl
{
    static TValue<SMI> remap_hash_sentinel(int64_t hash)
    {
        return TValue<SMI>::from_smi(hash == -1 ? -2 : hash);
    }

    Expected<TValue<SMI>> canonicalize_hash_result(Value value)
    {
        if(value.is_bool())
        {
            Value bool_hash;
            bool_hash.as.integer =
                value.as.integer & value_boolean_to_integer_mask;
            return Expected<TValue<SMI>>::ok(
                TValue<SMI>::from_value_unchecked(bool_hash));
        }
        if(value.is_smi())
        {
            return Expected<TValue<SMI>>::ok(
                remap_hash_sentinel(value.get_smi()));
        }
        if(can_convert_to<BigInt>(value))
        {
            TValue<SMI> hash = bigint_hash(value.get_ptr<BigInt>()->view(),
                                           uint64_t(clover_hash_modulus));
            return Expected<TValue<SMI>>::ok(
                remap_hash_sentinel(hash.extract()));
        }

        return Expected<TValue<SMI>>::raise_exception(
            L"TypeError", L"__hash__ method should return an integer");
    }

    TValue<SMI> canonicalize_nonnegative_raw_hash(uint64_t hash)
    {
        return remap_hash_sentinel(
            static_cast<int64_t>(hash % uint64_t(clover_hash_modulus)));
    }

}  // namespace cl
