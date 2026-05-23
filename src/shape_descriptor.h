#ifndef CL_SHAPE_DESCRIPTOR_H
#define CL_SHAPE_DESCRIPTOR_H

#include <cassert>
#include <cstdint>

namespace cl
{
    enum class StorageKind : uint8_t
    {
        Inline,
        Overflow,
    };

    class StorageLocation
    {
    public:
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
        SpecialRead = 1 << 2,
        SpecialMutate = 1 << 3,
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

    enum class DescriptorSpecialKind : uint8_t
    {
        None = 0,
        ShapeClass,
        SlotDict,
        ModuleBuiltins,
    };

    class DescriptorInfo
    {
    public:
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
             DescriptorFlags flags = descriptor_flag(DescriptorFlag::None),
             DescriptorSpecialKind special_kind = DescriptorSpecialKind::None)
        {
            bool has_special_flags =
                has_descriptor_flag(flags, DescriptorFlag::SpecialRead) ||
                has_descriptor_flag(flags, DescriptorFlag::SpecialMutate);
            assert(has_special_flags ==
                   (special_kind != DescriptorSpecialKind::None));
            return DescriptorInfo{location.physical_idx, location.kind,
                                  static_cast<uint8_t>(special_kind), flags};
        }

        StorageLocation storage_location() const
        {
            return StorageLocation{physical_idx, kind};
        }

        bool has_flag(DescriptorFlag flag) const
        {
            return has_descriptor_flag(flags, flag);
        }

        DescriptorSpecialKind special_kind() const
        {
            return static_cast<DescriptorSpecialKind>(reserved);
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

    class DescriptorLookup
    {
    public:
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
