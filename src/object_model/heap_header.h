#ifndef CL_HEAP_HEADER_H
#define CL_HEAP_HEADER_H

#include "memory/native_layout_id.h"
#include "object_model/value_tags.h"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace cl
{
    class HeapObject;

    enum class HeapLifecycleState : uint8_t
    {
        Normal = 0,
        InZct = 1,
        Reclaiming = 2,
        Dead = 3,
    };

    enum class HeapGenerationState : uint8_t
    {
        Young = 0x01,
        Old = 0x02,
        OldRemembered = 0x03,
    };

    // Normal metadata and a generation-preserving forwarding word share
    // storage. This representation does not authorize forwarding live RC
    // objects.
    struct alignas(uintptr_t) HeapHeader
    {
        explicit HeapHeader(NativeLayoutId layout, uint16_t aux = 0)
            : state_(static_cast<uint8_t>(HeapGenerationState::Old)),
              layout_(layout), aux_(aux), rc_(0)
        {
            static_assert(offsetof(HeapHeader, state_) == 0);
            static_assert(offsetof(HeapHeader, layout_) == 1);
            static_assert(offsetof(HeapHeader, aux_) == 2);
            static_assert(offsetof(HeapHeader, rc_) == 4);
        }

        bool is_forwarded() const { return (state_ & forwarded_bit) != 0; }

        HeapGenerationState generation() const
        {
            uint8_t bits = state_ & generation_mask;
            assert(bits >= static_cast<uint8_t>(HeapGenerationState::Young) &&
                   bits <= static_cast<uint8_t>(
                               HeapGenerationState::OldRemembered));
            return static_cast<HeapGenerationState>(bits);
        }

        void set_generation(HeapGenerationState state)
        {
            assert(!is_forwarded());
            uint8_t bits = static_cast<uint8_t>(state);
            assert(bits >= static_cast<uint8_t>(HeapGenerationState::Young) &&
                   bits <= static_cast<uint8_t>(
                               HeapGenerationState::OldRemembered));
            state_ = (state_ & ~generation_mask) | bits;
        }

        HeapLifecycleState lifecycle() const
        {
            assert(!is_forwarded());
            return static_cast<HeapLifecycleState>((state_ & lifecycle_mask) >>
                                                   lifecycle_shift);
        }

        void set_lifecycle(HeapLifecycleState state)
        {
            assert(!is_forwarded());
            uint8_t bits = static_cast<uint8_t>(state);
            assert(bits <= static_cast<uint8_t>(HeapLifecycleState::Dead));
            state_ = (state_ & ~lifecycle_mask) | (bits << lifecycle_shift);
        }

        NativeLayoutId native_layout() const
        {
            assert(!is_forwarded());
            return layout_;
        }

        uint16_t aux_count() const
        {
            assert(!is_forwarded());
            return aux_;
        }

        void set_aux_count(uint16_t count)
        {
            assert(!is_forwarded());
            aux_ = count;
        }

        int32_t &refcount()
        {
            assert(!is_forwarded());
            return rc_;
        }

        int32_t refcount() const
        {
            assert(!is_forwarded());
            return rc_;
        }

        HeapObject *forwarding_target() const
        {
            assert(is_forwarded());
            uintptr_t encoded;
            std::memcpy(&encoded, this, sizeof(encoded));
            auto *target = reinterpret_cast<HeapObject *>(
                encoded & ~forwarding_metadata_mask);
            assert(target != nullptr);
            return target;
        }

        // Collector-only: only source generation survives this store. It must
        // not copy normal lifecycle/spare bits into the target address.
        void install_forwarding_target(HeapObject *target)
        {
            assert(!is_forwarded());
            assert(target != nullptr);
            uintptr_t address = reinterpret_cast<uintptr_t>(target);
            assert((address & forwarding_metadata_mask) == 0);
            uintptr_t encoded =
                address | forwarded_bit | static_cast<uintptr_t>(generation());
            std::memcpy(this, &encoded, sizeof(encoded));
        }

    private:
        static constexpr uint8_t generation_mask = 0x03;
        static constexpr uint8_t forwarded_bit = 0x04;
        static constexpr uintptr_t forwarding_metadata_mask =
            generation_mask | forwarded_bit;
        static constexpr uint8_t lifecycle_mask = 0xc0;
        static constexpr unsigned lifecycle_shift = 6;

        uint8_t state_;
        NativeLayoutId layout_;
        uint16_t aux_;
        int32_t rc_;
    };

    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(HeapObject *) == sizeof(uint64_t));
    static_assert((value_refcounted_ptr_tag & 0x07) == 0);
    static_assert((value_interned_ptr_tag & 0x07) == 0);
    static_assert(sizeof(HeapHeader) == 8);
    static_assert(sizeof(uintptr_t) == sizeof(HeapHeader));
    static_assert(alignof(HeapHeader) == 8);
    static_assert(std::is_standard_layout_v<HeapHeader>);
    static_assert(std::is_trivially_copyable_v<HeapHeader>);
    static_assert(std::is_trivially_destructible_v<HeapHeader>);
}  // namespace cl

#endif  // CL_HEAP_HEADER_H
