#ifndef CL_JIT_MACHINE_ADDRESS_H
#define CL_JIT_MACHINE_ADDRESS_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cl::jit
{
    namespace detail
    {
        class MachineAddressAccess;
    }

    class MachineAddress
    {
    public:
        MachineAddress() = delete;

        MachineAddress offset_by(size_t bytes) const
        {
            assert(bytes <= std::numeric_limits<uintptr_t>::max() - bits_);
            return MachineAddress(bits_ + bytes);
        }

        int64_t displacement_to(MachineAddress target) const
        {
            if(target.bits_ >= bits_)
            {
                return checked_positive_displacement(target.bits_ - bits_);
            }

            return -checked_positive_displacement(bits_ - target.bits_);
        }

        int64_t aligned_displacement_to(MachineAddress target,
                                        uint8_t alignment_shift) const
        {
            assert(alignment_shift < std::numeric_limits<uintptr_t>::digits);
            uintptr_t source_units = bits_ >> alignment_shift;
            uintptr_t target_units = target.bits_ >> alignment_shift;
            uintptr_t magnitude_units = source_units <= target_units
                                            ? target_units - source_units
                                            : source_units - target_units;
            assert(
                magnitude_units <=
                (static_cast<uintptr_t>(std::numeric_limits<int64_t>::max()) >>
                 alignment_shift));
            int64_t magnitude =
                static_cast<int64_t>(magnitude_units << alignment_shift);
            return source_units <= target_units ? magnitude : -magnitude;
        }

        size_t offset_within(uint8_t alignment_shift) const
        {
            assert(alignment_shift < std::numeric_limits<uintptr_t>::digits);
            uintptr_t mask = (uintptr_t{1} << alignment_shift) - 1;
            return static_cast<size_t>(bits_ & mask);
        }

        uintptr_t bits_for_indirect_target() const { return bits_; }

        bool operator==(MachineAddress other) const
        {
            return bits_ == other.bits_;
        }

        bool operator!=(MachineAddress other) const
        {
            return !(*this == other);
        }

    private:
        friend class detail::MachineAddressAccess;

        explicit MachineAddress(uintptr_t bits) : bits_(bits)
        {
            assert(bits != 0);
        }

        static int64_t checked_positive_displacement(uintptr_t magnitude)
        {
            assert(magnitude <=
                   static_cast<uintptr_t>(std::numeric_limits<int64_t>::max()));
            return static_cast<int64_t>(magnitude);
        }

        uintptr_t bits_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_MACHINE_ADDRESS_H
