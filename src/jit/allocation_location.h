#ifndef CL_JIT_ALLOCATION_LOCATION_H
#define CL_JIT_ALLOCATION_LOCATION_H

#include "jit/physical_register.h"
#include "runtime/fatal.h"

#include <cstdint>
#include <variant>

namespace cl::jit
{
    enum class StackLocationKind : uint8_t
    {
        IncomingParameter,
        LocalOrTemporary,
        OutgoingCallArgument,
        SpillSlot,
    };

    class StackLocation
    {
    public:
        constexpr StackLocation(StackLocationKind kind, int32_t frame_offset)
            : kind_(kind), frame_offset_(frame_offset)
        {
        }

        constexpr StackLocationKind kind() const { return kind_; }
        constexpr int32_t frame_offset() const { return frame_offset_; }

        constexpr bool aliases(const StackLocation &other) const
        {
            return frame_offset_ == other.frame_offset_;
        }

    private:
        StackLocationKind kind_;
        int32_t frame_offset_;
    };

    class AllocationLocation
    {
    public:
        static AllocationLocation reg(PhysicalRegister reg)
        {
            return AllocationLocation(reg);
        }

        static AllocationLocation stack(StackLocation stack)
        {
            return AllocationLocation(stack);
        }

        bool is_register() const
        {
            return std::holds_alternative<PhysicalRegister>(storage_);
        }

        bool is_stack() const
        {
            return std::holds_alternative<StackLocation>(storage_);
        }

        PhysicalRegister reg() const
        {
            if(!is_register())
            {
                fatal("reg() requires a register allocation location");
            }
            return std::get<PhysicalRegister>(storage_);
        }

        StackLocation stack() const
        {
            if(!is_stack())
            {
                fatal("stack() requires a stack allocation location");
            }
            return std::get<StackLocation>(storage_);
        }

        bool aliases(const AllocationLocation &other) const
        {
            if(is_register() && other.is_register())
            {
                return reg() == other.reg();
            }
            if(is_stack() && other.is_stack())
            {
                return stack().aliases(other.stack());
            }
            return false;
        }

    private:
        explicit AllocationLocation(PhysicalRegister reg) : storage_(reg) {}
        explicit AllocationLocation(StackLocation stack) : storage_(stack) {}

        std::variant<PhysicalRegister, StackLocation> storage_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_ALLOCATION_LOCATION_H
