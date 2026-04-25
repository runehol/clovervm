#ifndef CL_SHAPE_DESCRIPTOR_H
#define CL_SHAPE_DESCRIPTOR_H

#include <cstdint>

namespace cl
{
    enum class StorageKind : uint8_t
    {
        Inline,
        Overflow,
    };

    struct StorageLocation
    {
        int32_t physical_idx;
        StorageKind kind;

        static StorageLocation not_found()
        {
            return StorageLocation{-1, StorageKind::Inline};
        }

        bool is_found() const { return physical_idx >= 0; }
    };

    enum class DescriptorFlag : uint16_t
    {
        None = 0,
        ReadOnly = 1 << 0,
        StableSlot = 1 << 1,
    };

    using DescriptorFlags = uint16_t;

    constexpr DescriptorFlags descriptor_flag(DescriptorFlag flag)
    {
        return static_cast<DescriptorFlags>(flag);
    }

    constexpr bool has_descriptor_flag(DescriptorFlags flags,
                                       DescriptorFlag flag)
    {
        return (flags & descriptor_flag(flag)) != 0;
    }

    struct DescriptorInfo
    {
        int32_t physical_idx;
        StorageKind kind;
        uint8_t reserved;
        DescriptorFlags flags;

        static DescriptorInfo not_found()
        {
            return DescriptorInfo{-1, StorageKind::Inline, 0,
                                  descriptor_flag(DescriptorFlag::None)};
        }

        static DescriptorInfo
        make(StorageLocation location,
             DescriptorFlags flags = descriptor_flag(DescriptorFlag::None))
        {
            return DescriptorInfo{location.physical_idx, location.kind, 0,
                                  flags};
        }

        StorageLocation storage_location() const
        {
            return StorageLocation{physical_idx, kind};
        }

        bool has_flag(DescriptorFlag flag) const
        {
            return has_descriptor_flag(flags, flag);
        }
    };

    static_assert(sizeof(DescriptorInfo) == 8,
                  "DescriptorInfo should stay packed into 64 bits");

    enum class DescriptorPresence : uint8_t
    {
        Absent,
        Present,
        Latent,
    };

    struct DescriptorLookup
    {
        DescriptorPresence presence;
        int32_t descriptor_idx;
        DescriptorInfo info;

        static DescriptorLookup absent()
        {
            return DescriptorLookup{DescriptorPresence::Absent, -1,
                                    DescriptorInfo::not_found()};
        }

        bool is_present() const
        {
            return presence == DescriptorPresence::Present;
        }

        bool is_latent() const
        {
            return presence == DescriptorPresence::Latent;
        }

        StorageLocation storage_location() const
        {
            return info.storage_location();
        }
    };

}  // namespace cl

#endif  // CL_SHAPE_DESCRIPTOR_H
