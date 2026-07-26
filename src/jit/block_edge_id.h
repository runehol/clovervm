#ifndef CL_JIT_BLOCK_EDGE_ID_H
#define CL_JIT_BLOCK_EDGE_ID_H

#include <cstdint>
#include <utility>

namespace cl::jit
{
    class BlockEdgeId
    {
    public:
        explicit constexpr BlockEdgeId(uint32_t value) : value_(value) {}

        constexpr uint32_t value() const { return value_; }

        friend bool operator==(BlockEdgeId, BlockEdgeId) = default;

        template <typename H> friend H AbslHashValue(H hash, BlockEdgeId id)
        {
            return H::combine(std::move(hash), id.value_);
        }

    private:
        uint32_t value_;
    };

    static_assert(sizeof(BlockEdgeId) == sizeof(uint32_t));

}  // namespace cl::jit

#endif  // CL_JIT_BLOCK_EDGE_ID_H
