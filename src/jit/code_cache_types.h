#ifndef CL_JIT_CODE_CACHE_TYPES_H
#define CL_JIT_CODE_CACHE_TYPES_H

#include "jit/machine_address.h"
#include "object_model/value.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace cl::jit
{
    enum class JitCodeError : uint8_t
    {
        PoolOutOfRange,
        AllocationFailure,
        PublicationFailure,
    };

    class CodeSlice
    {
    public:
        CodeSlice(void *write_pointer, MachineAddress execute_address,
                  size_t capacity)
            : write_pointer_(write_pointer), execute_address_(execute_address),
              capacity_(capacity)
        {
            assert(write_pointer != nullptr);
            assert(reinterpret_cast<uintptr_t>(write_pointer) % 16 == 0);
            assert(execute_address.offset_within(4) == 0);
        }

        void *write_pointer() const { return write_pointer_; }
        MachineAddress execute_address() const { return execute_address_; }
        size_t capacity() const { return capacity_; }

    private:
        void *write_pointer_;
        MachineAddress execute_address_;
        size_t capacity_;
    };

    class ValuePoolSlice
    {
    public:
        ValuePoolSlice(Value *write_pointer, MachineAddress address,
                       size_t slot_count)
            : write_pointer_(write_pointer), address_(address),
              slot_count_(slot_count)
        {
            assert(write_pointer != nullptr);
            assert(reinterpret_cast<uintptr_t>(write_pointer) % sizeof(Value) ==
                   0);
            assert(address.offset_within(value_alignment_shift()) == 0);
        }

        Value *write_pointer() const { return write_pointer_; }
        MachineAddress address() const { return address_; }
        size_t slot_count() const { return slot_count_; }

    private:
        static constexpr uint8_t value_alignment_shift()
        {
            static_assert(sizeof(Value) != 0);
            static_assert((sizeof(Value) & (sizeof(Value) - 1)) == 0);
            uint8_t shift = 0;
            size_t alignment = sizeof(Value);
            while(alignment > 1)
            {
                alignment >>= 1;
                ++shift;
            }
            return shift;
        }

        Value *write_pointer_;
        MachineAddress address_;
        size_t slot_count_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CODE_CACHE_TYPES_H
