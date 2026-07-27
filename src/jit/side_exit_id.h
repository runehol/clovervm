#ifndef CL_JIT_SIDE_EXIT_ID_H
#define CL_JIT_SIDE_EXIT_ID_H

#include <cstdint>
#include <utility>

namespace cl::jit
{
    class SideExitId
    {
    public:
        explicit constexpr SideExitId(uint32_t value) : value_(value) {}

        constexpr uint32_t value() const { return value_; }

        friend bool operator==(SideExitId, SideExitId) = default;

        template <typename H> friend H AbslHashValue(H hash, SideExitId id)
        {
            return H::combine(std::move(hash), id.value_);
        }

    private:
        uint32_t value_;
    };

    static_assert(sizeof(SideExitId) == sizeof(uint32_t));

}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_ID_H
