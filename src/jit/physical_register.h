#ifndef CL_JIT_PHYSICAL_REGISTER_H
#define CL_JIT_PHYSICAL_REGISTER_H

#include "runtime/fatal.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cl::jit
{
    enum class RegisterClass : uint8_t
    {
        GPR,
        SIMD,
        Count,
    };

    class PhysicalRegister
    {
    public:
        static constexpr size_t MaxRegistersPerClass = 64;

        constexpr PhysicalRegister(RegisterClass register_class, uint8_t number)
            : register_class_(register_class), number_(number)
        {
            if(register_class >= RegisterClass::Count)
            {
                fatal("invalid JIT register class");
            }
            if(number >= MaxRegistersPerClass)
            {
                fatal("JIT physical register number exceeds class limit");
            }
        }

        constexpr RegisterClass register_class() const
        {
            return register_class_;
        }

        constexpr uint8_t number() const { return number_; }

        friend constexpr bool operator==(PhysicalRegister,
                                         PhysicalRegister) = default;

    private:
        RegisterClass register_class_;
        uint8_t number_;
    };

    class RegisterSet
    {
    public:
        static constexpr size_t MaxRegistersPerClass =
            PhysicalRegister::MaxRegistersPerClass;

        bool contains(PhysicalRegister reg) const
        {
            return members_[class_index(reg.register_class())].test(
                reg.number());
        }

        void insert(PhysicalRegister reg)
        {
            members_[class_index(reg.register_class())].set(reg.number());
        }

        void erase(PhysicalRegister reg)
        {
            members_[class_index(reg.register_class())].reset(reg.number());
        }

        size_t size() const
        {
            size_t result = 0;
            for(const auto &members: members_)
            {
                result += members.count();
            }
            return result;
        }

        friend bool operator==(RegisterSet, RegisterSet) = default;

    private:
        static constexpr size_t class_index(RegisterClass register_class)
        {
            return static_cast<size_t>(register_class);
        }

        std::array<std::bitset<MaxRegistersPerClass>,
                   static_cast<size_t>(RegisterClass::Count)>
            members_{};
    };

    struct RegisterClassDefinition
    {
        RegisterClassDefinition(
            RegisterClass register_class, RegisterSet members,
            std::span<const PhysicalRegister> allocation_order);

        RegisterClass register_class;
        RegisterSet members;
        std::span<const PhysicalRegister> allocation_order;
    };

}  // namespace cl::jit

#endif  // CL_JIT_PHYSICAL_REGISTER_H
