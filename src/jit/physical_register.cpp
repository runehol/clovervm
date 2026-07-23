#include "jit/physical_register.h"

#include "runtime/fatal.h"

namespace cl::jit
{
    RegisterClassDefinition::RegisterClassDefinition(
        RegisterClass register_class, RegisterSet members,
        std::span<const PhysicalRegister> allocation_order)
        : register_class(register_class), members(members),
          allocation_order(allocation_order)
    {
        if(register_class >= RegisterClass::Count)
        {
            fatal("invalid JIT register class definition");
        }
        if(allocation_order.size() != members.size())
        {
            fatal("JIT register allocation order does not contain every "
                  "register exactly once");
        }

        RegisterSet seen;
        for(PhysicalRegister reg: allocation_order)
        {
            if(reg.register_class() != register_class)
            {
                fatal("JIT register allocation order contains the wrong "
                      "register class");
            }
            if(!members.contains(reg))
            {
                fatal("JIT register allocation order contains a non-member");
            }
            if(seen.contains(reg))
            {
                fatal("JIT register allocation order contains a duplicate");
            }
            seen.insert(reg);
        }
    }

}  // namespace cl::jit
