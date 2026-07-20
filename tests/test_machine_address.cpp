#include "jit/code_cache_types.h"
#include "jit/machine_address.h"
#include "jit/machine_address_internal.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace cl::jit
{
    namespace
    {
        MachineAddress address(uintptr_t bits)
        {
            return detail::MachineAddressAccess::from_bits(bits);
        }
    }  // namespace

    TEST(MachineAddress, IsOpaqueAndHasNoNullState)
    {
        static_assert(!std::is_default_constructible_v<MachineAddress>);
        static_assert(!std::is_constructible_v<MachineAddress, uintptr_t>);
        static_assert(!std::is_constructible_v<MachineAddress, void *>);
        static_assert(!std::is_convertible_v<MachineAddress, uintptr_t>);
        static_assert(!std::is_convertible_v<MachineAddress, void *>);
    }

    TEST(MachineAddress, CacheAccessConstructsFromMappedPointers)
    {
        alignas(16) uint8_t storage[16] = {};

        EXPECT_EQ(reinterpret_cast<uintptr_t>(storage),
                  detail::MachineAddressAccess::from_pointer(storage)
                      .bits_for_indirect_target());
        EXPECT_DEATH(detail::MachineAddressAccess::from_pointer(nullptr),
                     "pointer != nullptr");
    }

    TEST(MachineAddress, AdvancesAndComputesSignedDisplacements)
    {
        MachineAddress base = address(0x1000);

        EXPECT_EQ(address(0x1123), base.offset_by(0x123));
        EXPECT_EQ(0x234, base.displacement_to(address(0x1234)));
        EXPECT_EQ(-0x234, address(0x1234).displacement_to(base));
        EXPECT_EQ(0, base.displacement_to(base));
    }

    TEST(MachineAddress, ComputesAlignedByteDisplacementsFromShifts)
    {
        MachineAddress source = address(0x1234);
        MachineAddress target = address(0x4567);

        EXPECT_EQ(0x3000, source.aligned_displacement_to(target, 12));
        EXPECT_EQ(-0x3000, target.aligned_displacement_to(source, 12));
        EXPECT_EQ(0x234u, source.offset_within(12));
        EXPECT_EQ(0u, source.offset_within(0));
    }

    TEST(MachineAddress, ExposesBitsOnlyForIndirectTargets)
    {
        EXPECT_EQ(0x12345678u, address(0x12345678).bits_for_indirect_target());
    }

    TEST(MachineAddress, RejectsInvalidOrOverflowingOperations)
    {
        constexpr uint8_t address_bits = std::numeric_limits<uintptr_t>::digits;

        EXPECT_DEATH(address(0), "bits != 0");
        EXPECT_DEATH(
            address(std::numeric_limits<uintptr_t>::max()).offset_by(1),
            "bytes <=");
        EXPECT_DEATH(address(1).offset_within(address_bits),
                     "alignment_shift <");
        EXPECT_DEATH(
            address(1).aligned_displacement_to(address(2), address_bits),
            "alignment_shift <");

        if constexpr(sizeof(uintptr_t) == sizeof(uint64_t))
        {
            EXPECT_DEATH(address(1).displacement_to(
                             address(std::numeric_limits<uintptr_t>::max())),
                         "magnitude <=");
        }
    }

    TEST(CodeCacheTypes, DescribesExecutableCode)
    {
        CodeSlice code(address(0x4000), 32);

        EXPECT_EQ(address(0x4000), code.execute_address());
        EXPECT_EQ(32u, code.capacity());
    }

    TEST(CodeCacheTypes, DescribesGcVisiblePoolSlots)
    {
        alignas(sizeof(Value)) uint8_t pool_storage[sizeof(Value) * 2] = {};
        auto *pool_values = reinterpret_cast<Value *>(pool_storage);
        ValuePoolSlice pool(pool_values, address(0x8000), 2);

        EXPECT_EQ(pool_values, pool.write_pointer());
        EXPECT_EQ(address(0x8000), pool.address());
        EXPECT_EQ(2u, pool.slot_count());
    }

}  // namespace cl::jit
