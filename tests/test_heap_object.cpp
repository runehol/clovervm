#include "builtin_types/float.h"
#include "object_model/heap_header.h"
#include "object_model/heap_object.h"
#include "object_model/owned.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

using namespace cl;

namespace
{
    using HeaderBytes = std::array<uint8_t, 8>;

    uint8_t state_byte(const HeapHeader &header)
    {
        return std::bit_cast<HeaderBytes>(header)[0];
    }

    void set_state_byte(HeapHeader &header, uint8_t state)
    {
        HeaderBytes bytes = std::bit_cast<HeaderBytes>(header);
        bytes[0] = state;
        std::memcpy(&header, bytes.data(), sizeof(header));
    }

    constexpr std::array generations{HeapGenerationState::Young,
                                     HeapGenerationState::Old,
                                     HeapGenerationState::OldRemembered};
    constexpr std::array lifecycles{
        HeapLifecycleState::Normal, HeapLifecycleState::InZct,
        HeapLifecycleState::Reclaiming, HeapLifecycleState::Dead};
}  // namespace

TEST(HeapObjectHeader, EightByteLayoutKeepsFloatPayloadAndStride)
{
    EXPECT_EQ(8u, sizeof(HeapObject));
    EXPECT_EQ(8u, alignof(HeapObject));
    EXPECT_EQ(8u, sizeof(HeapHeader));
    EXPECT_EQ(8u, alignof(HeapHeader));
    HeapHeader header(NativeLayoutId::TestOnly, 0xbeef);
    header.refcount() = 0x12345678;
    HeaderBytes expected{0x02, static_cast<uint8_t>(NativeLayoutId::TestOnly),
                         0xef, 0xbe,
                         0x78, 0x56,
                         0x34, 0x12};
    EXPECT_EQ(expected, std::bit_cast<HeaderBytes>(header));
    EXPECT_EQ(32u, sizeof(Float));
    EXPECT_EQ(24u, Float::value_offset());
    EXPECT_EQ(32u, (sizeof(Float) + value_ptr_granularity - 1) &
                       ~(value_ptr_granularity - 1));
}

TEST(HeapObjectHeader, ConstructorsInitializeOldNormalRatherThanForwarding)
{
    HeapObject bootstrap;
    EXPECT_FALSE(bootstrap.is_forwarded());
    EXPECT_FALSE(bootstrap.is_non_moving());
    EXPECT_EQ(0x02u, std::bit_cast<HeaderBytes>(bootstrap)[0]);
    EXPECT_EQ(HeapGenerationState::Old, bootstrap.generation_state());
    EXPECT_EQ(HeapLifecycleState::Normal, bootstrap.lifecycle_state());
    EXPECT_EQ(NativeLayoutId::Invalid, bootstrap.native_layout_id());
    EXPECT_EQ(0u, bootstrap.native_layout_aux_count_value());
    EXPECT_EQ(0, bootstrap.refcount());

    HeapObject ordinary(NativeLayoutId::TestOnly, 0xabcd);
    EXPECT_FALSE(ordinary.is_forwarded());
    EXPECT_FALSE(ordinary.is_non_moving());
    EXPECT_EQ(0x02u, std::bit_cast<HeaderBytes>(ordinary)[0]);
    EXPECT_EQ(HeapGenerationState::Old, ordinary.generation_state());
    EXPECT_EQ(HeapLifecycleState::Normal, ordinary.lifecycle_state());
    EXPECT_EQ(NativeLayoutId::TestOnly, ordinary.native_layout_id());
    EXPECT_EQ(0xabcdu, ordinary.native_layout_aux_count_value());
    EXPECT_EQ(0, ordinary.refcount());
}

TEST(HeapObjectHeader, GenerationAndLifecycleUpdatesPreserveOtherState)
{
    for(HeapGenerationState generation: generations)
    {
        for(HeapLifecycleState lifecycle: lifecycles)
        {
            HeapHeader object(NativeLayoutId::TestOnly, 0xbeef);
            object.refcount() = 123;
            set_state_byte(object, state_byte(object) | 0x20);  // Spare bit 5.
            object.make_non_moving();
            object.set_generation(generation);
            object.set_lifecycle(lifecycle);

            for(HeapGenerationState next: generations)
            {
                object.set_generation(next);
                EXPECT_EQ(next, object.generation());
                EXPECT_EQ(lifecycle, object.lifecycle());
                EXPECT_FALSE(object.is_forwarded());
                EXPECT_TRUE(object.is_non_moving());
                EXPECT_EQ(0x24u, state_byte(object) & 0x3c);
            }
            object.set_generation(generation);
            for(HeapLifecycleState next: lifecycles)
            {
                object.set_lifecycle(next);
                EXPECT_EQ(generation, object.generation());
                EXPECT_EQ(next, object.lifecycle());
                EXPECT_FALSE(object.is_forwarded());
                EXPECT_TRUE(object.is_non_moving());
                EXPECT_EQ(0x24u, state_byte(object) & 0x3c);
            }
            EXPECT_EQ(NativeLayoutId::TestOnly, object.native_layout());
            EXPECT_EQ(0xbeefu, object.aux_count());
            EXPECT_EQ(123, object.refcount());
        }
    }
}

TEST(HeapObjectHeader, NonMovingMarkIsIdempotentAndPreservesNormalMetadata)
{
    for(HeapGenerationState generation: generations)
    {
        for(HeapLifecycleState lifecycle: lifecycles)
        {
            HeapHeader header(NativeLayoutId::TestOnly, 0xbeef);
            header.set_generation(generation);
            header.set_lifecycle(lifecycle);
            header.refcount() = 123;
            set_state_byte(header, state_byte(header) | 0x20);
            HeaderBytes expected = std::bit_cast<HeaderBytes>(header);
            expected[0] |= 0x04;
            header.make_non_moving();
            EXPECT_TRUE(header.is_non_moving());
            EXPECT_FALSE(header.is_forwarded());
            EXPECT_EQ(expected, std::bit_cast<HeaderBytes>(header));
            header.make_non_moving();
            EXPECT_EQ(expected, std::bit_cast<HeaderBytes>(header));
        }
    }
}

TEST(HeapObjectHeader, ForwardedDiscriminatorUsesPointerTagsForAllStateBytes)
{
    size_t forwarded = 0;
    for(unsigned value = 0; value < 256; ++value)
    {
        HeapHeader object(NativeLayoutId::TestOnly);
        set_state_byte(object, static_cast<uint8_t>(value));
        // The discriminator is independent of generation validity. Generation
        // zero and malformed pointer tags remain invalid representations.
        EXPECT_EQ((value & value_ptr_mask) != 0, object.is_forwarded());
        forwarded += object.is_forwarded();
    }
    EXPECT_EQ(192u, forwarded);
}

TEST(HeapObjectHeader, ForwardingPreservesGenerationAndRecoversExactPointer)
{
    alignas(256) std::array<std::byte, 256> target_storage{};
    for(size_t offset = 0; offset < target_storage.size(); offset += 8)
    {
        // Synthetic aligned pointer values are never dereferenced. These
        // disposable source headers never enter production RC bookkeeping.
        // Only VM-tagged targets are admissible. Exercise both tags and all
        // higher low-byte address bits, including bits 6--7 (not RC lifecycle).
        uintptr_t tag = offset & value_tag_mask;
        if(tag != value_refcounted_ptr_tag && tag != value_interned_ptr_tag)
        {
            continue;
        }
        auto *target =
            reinterpret_cast<HeapObject *>(target_storage.data() + offset);
        for(HeapGenerationState generation: generations)
        {
            for(HeapLifecycleState lifecycle: lifecycles)
            {
                HeapHeader source(NativeLayoutId::TestOnly, 0xbeef);
                source.refcount() = 0x12345678;
                source.set_generation(generation);
                source.set_lifecycle(lifecycle);
                // Spare flags and lifecycle must not leak into the target.
                set_state_byte(source, state_byte(source) | 0x20);
                source.install_forwarding_target(target);
                EXPECT_TRUE(source.is_forwarded());
                EXPECT_FALSE(source.is_non_moving());
                EXPECT_EQ(generation, source.generation());
                EXPECT_EQ(target, source.forwarding_target());
                uintptr_t stored;
                std::memcpy(&stored, &source, sizeof(stored));
                uintptr_t expected = reinterpret_cast<uintptr_t>(target) |
                                     static_cast<uintptr_t>(generation);
                EXPECT_EQ(expected, stored);
                EXPECT_EQ(static_cast<uint8_t>(expected), state_byte(source));
            }
        }
    }
}

TEST(HeapObjectHeader, ObjectForwardingTouchesOnlyContainedHeader)
{
    struct Record : HeapObject
    {
        Record() : HeapObject(NativeLayoutId::TestOnly) {}
        uint64_t payload = 0x0123456789abcdef;
    };
    Record source;
    alignas(32) std::array<std::byte, 32 + sizeof(Record)> storage;
    Record *destination = std::construct_at(
        reinterpret_cast<Record *>(storage.data() + value_refcounted_ptr_tag));
    destination->make_non_moving();
    source.set_generation_state(HeapGenerationState::Young);
    source.install_forwarding_target(destination);
    EXPECT_TRUE(source.is_forwarded());
    EXPECT_EQ(HeapGenerationState::Young, source.generation_state());
    EXPECT_FALSE(source.is_non_moving());
    EXPECT_EQ(destination, source.forwarding_target());
    EXPECT_EQ(uint64_t{0x0123456789abcdef}, source.payload);
    EXPECT_FALSE(destination->is_forwarded());
    EXPECT_TRUE(destination->is_non_moving());
    EXPECT_EQ(HeapGenerationState::Old, destination->generation_state());
    EXPECT_EQ(uint64_t{0x0123456789abcdef}, destination->payload);
    std::destroy_at(destination);
}

TEST(HeapObjectHeader, RealRefcountAndZctUpdatesPreserveGenerationAndMetadata)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Float *raw = context.thread()->make_object_raw<Float>(1.5);
    // Synthetic metadata exercise only: current RC allocation never evacuates.
    raw->make_non_moving();
    {
        Owned<TValue<Float>> owner(TValue<Float>::from_oop(raw));
        raw->set_generation_state(HeapGenerationState::Young);
        std::array<std::byte, 4> prefix;
        std::memcpy(prefix.data(), raw, prefix.size());
        EXPECT_EQ(1, raw->refcount());
        {
            Owned<TValue<Float>> second(owner.value());
            EXPECT_EQ(2, raw->refcount());
            std::array<std::byte, 4> current;
            std::memcpy(current.data(), raw, current.size());
            EXPECT_EQ(prefix, current);
        }
        EXPECT_EQ(1, raw->refcount());
        EXPECT_EQ(HeapGenerationState::Young, raw->generation_state());
        EXPECT_EQ(HeapLifecycleState::Normal, raw->lifecycle_state());
    }
    // No allocation or safepoint between releasing the final owner and these
    // reads. ZCT publication changes lifecycle but preserves generation.
    EXPECT_EQ(0, raw->refcount());
    EXPECT_EQ(HeapLifecycleState::InZct, raw->lifecycle_state());
    EXPECT_TRUE(raw->is_non_moving());
    EXPECT_EQ(0x45u,
              std::bit_cast<HeaderBytes>(*static_cast<HeapObject *>(raw))[0]);
    EXPECT_EQ(HeapGenerationState::Young, raw->generation_state());
    EXPECT_EQ(Float::native_layout, raw->native_layout_id());
    EXPECT_EQ(0u, raw->native_layout_aux_count_value());
}

#ifndef NDEBUG
TEST(HeapObjectHeaderDeathTest, NormalOnlyAccessorsRejectForwardedHeaders)
{
    HeapHeader source(NativeLayoutId::TestOnly);
    alignas(32) std::array<std::byte, 32> storage;
    auto *destination = reinterpret_cast<HeapObject *>(
        storage.data() + value_refcounted_ptr_tag);
    source.install_forwarding_target(destination);
    const HeapHeader &constant = source;
    EXPECT_EQ(HeapGenerationState::Old, source.generation());
    EXPECT_FALSE(source.is_non_moving());
    EXPECT_EQ(destination, source.forwarding_target());
    EXPECT_DEATH(source.make_non_moving(), "!is_forwarded");
    EXPECT_DEATH((void)source.lifecycle(), "!is_forwarded");
    EXPECT_DEATH(source.set_lifecycle(HeapLifecycleState::Normal),
                 "!is_forwarded");
    EXPECT_DEATH((void)source.native_layout(), "!is_forwarded");
    EXPECT_DEATH((void)source.aux_count(), "!is_forwarded");
    EXPECT_DEATH(source.set_aux_count(0), "!is_forwarded");
    EXPECT_DEATH((void)source.refcount(), "!is_forwarded");
    EXPECT_DEATH((void)constant.refcount(), "!is_forwarded");
    EXPECT_DEATH(source.set_generation(HeapGenerationState::Young),
                 "!is_forwarded");
    EXPECT_DEATH(source.install_forwarding_target(destination),
                 "!is_forwarded");
}

TEST(HeapObjectHeaderDeathTest, GenerationZeroIsInvalidInEitherForm)
{
    HeapHeader source(NativeLayoutId::TestOnly);
    set_state_byte(source, 0);
    EXPECT_FALSE(source.is_forwarded());
    EXPECT_DEATH((void)source.generation(), "bits >=");
    set_state_byte(source, 0x04);
    EXPECT_FALSE(source.is_forwarded());
    EXPECT_TRUE(source.is_non_moving());
    EXPECT_DEATH((void)source.generation(), "bits >=");
    set_state_byte(source, 0x10);
    EXPECT_TRUE(source.is_forwarded());
    EXPECT_DEATH((void)source.generation(), "bits >=");
}

TEST(HeapObjectHeaderDeathTest, ForwardingRequiresAMovableSourceAndTaggedTarget)
{
    HeapHeader source(NativeLayoutId::TestOnly);
    EXPECT_DEATH(source.install_forwarding_target(nullptr),
                 "target != nullptr");
    alignas(32) std::array<std::byte, 32> storage;
    for(size_t offset: {size_t{0}, size_t{17}, size_t{24}})
    {
        auto *invalid = reinterpret_cast<HeapObject *>(storage.data() + offset);
        EXPECT_DEATH(source.install_forwarding_target(invalid), "tag ==");
    }
    EXPECT_DEATH((void)source.forwarding_target(), "is_forwarded");
    source.make_non_moving();
    auto *valid = reinterpret_cast<HeapObject *>(storage.data() + 16);
    EXPECT_DEATH(source.install_forwarding_target(valid), "!is_non_moving");
}

TEST(HeapObjectHeaderDeathTest, ForwardingTargetRejectsMalformedPointerTags)
{
    HeapHeader source(NativeLayoutId::TestOnly);
    for(uint8_t state: {uint8_t{0x19}, uint8_t{0x15}})
    {
        set_state_byte(source, state);
        EXPECT_TRUE(source.is_forwarded());
        EXPECT_DEATH((void)source.forwarding_target(), "tag ==");
    }
}
#endif
