#ifndef CL_JIT_INSTRUCTION_POOL_H
#define CL_JIT_INSTRUCTION_POOL_H

#include "jit/instruction.h"

#include <span>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace cl::jit
{
    class InstructionPool
    {
    public:
        InstructionPool() = default;

        InstructionPool(const InstructionPool &) = delete;
        InstructionPool &operator=(const InstructionPool &) = delete;
        InstructionPool(InstructionPool &&) = delete;
        InstructionPool &operator=(InstructionPool &&) = delete;

        template <typename T, typename... Args> T *make(Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, T>);
            static_assert(sizeof(T) == sizeof(Instruction));
            static_assert(alignof(T) == alignof(Instruction));
            static_assert(std::is_trivially_destructible_v<T>);
            assert(next_serial_ != std::numeric_limits<uint32_t>::max());

            void *storage = allocate_record();
            return new(storage) T(next_serial_++, std::forward<Args>(args)...);
        }

    private:
        static constexpr size_t RecordsPerSlab = 64;
        static constexpr size_t SlabBytes =
            RecordsPerSlab * sizeof(Instruction) + alignof(Instruction) - 1;

        struct Slab
        {
            std::unique_ptr<std::byte[]> storage;
            std::byte *next = nullptr;
            size_t remaining = 0;
        };

        void add_slab();
        void *allocate_record();

        uint32_t next_serial_ = 0;
        std::vector<Slab> slabs_;
    };

    class InstructionSideDataPool
    {
    public:
        InstructionSideDataPool() = default;

        InstructionSideDataPool(const InstructionSideDataPool &) = delete;
        InstructionSideDataPool &
        operator=(const InstructionSideDataPool &) = delete;
        InstructionSideDataPool(InstructionSideDataPool &&) = delete;
        InstructionSideDataPool &operator=(InstructionSideDataPool &&) = delete;

        std::span<uintptr_t> allocate_words(size_t count);

    private:
        static constexpr size_t WordsPerSlab = 256;

        struct Slab
        {
            std::unique_ptr<uintptr_t[]> storage;
            size_t capacity = 0;
            size_t used = 0;
        };

        std::vector<Slab> slabs_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_POOL_H
