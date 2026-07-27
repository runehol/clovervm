#ifndef CL_UTIL_DENSE_ID_H
#define CL_UTIL_DENSE_ID_H

#include <compare>
#include <cstdint>
#include <utility>

namespace cl
{
    template <typename Entity> class DenseId
    {
    public:
        explicit constexpr DenseId(uint32_t value) : value_(value) {}

        constexpr uint32_t value() const { return value_; }

        friend constexpr auto operator<=>(DenseId, DenseId) = default;

        template <typename H> friend H AbslHashValue(H hash, DenseId id)
        {
            return H::combine(std::move(hash), id.value_);
        }

    private:
        uint32_t value_;
    };

    struct DenseIdSizeProbe;
    static_assert(sizeof(DenseId<DenseIdSizeProbe>) == sizeof(uint32_t));

}  // namespace cl

#endif  // CL_UTIL_DENSE_ID_H
