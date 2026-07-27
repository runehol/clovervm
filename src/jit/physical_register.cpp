#include "jit/physical_register.h"

#include "runtime/fatal.h"

namespace cl::jit
{
    RegisterClassDefinition::RegisterClassDefinition(
        RegisterClass register_class,
        std::span<const PhysicalRegister> allocation_order,
        std::span<const PhysicalRegister> scratch_registers)
        : register_class_(register_class),
          allocation_order_(allocation_order.begin(), allocation_order.end()),
          scratch_registers_(scratch_registers.begin(), scratch_registers.end())
    {
        if(register_class >= RegisterClass::Count)
        {
            fatal("invalid JIT register class definition");
        }

        for(PhysicalRegister reg: allocation_order_)
        {
            if(reg.register_class() != register_class)
            {
                fatal("JIT register allocation order contains the wrong "
                      "register class");
            }
            if(members_.contains(reg))
            {
                fatal("JIT register allocation order contains a duplicate");
            }
            members_.insert(reg);
        }

        RegisterSet seen_scratch;
        for(PhysicalRegister scratch: scratch_registers_)
        {
            if(scratch.register_class() != register_class)
            {
                fatal("JIT scratch register has the wrong register class");
            }
            if(members_.contains(scratch))
            {
                fatal("JIT scratch register is also allocatable");
            }
            if(seen_scratch.contains(scratch))
            {
                fatal("JIT scratch register list contains a duplicate");
            }
            seen_scratch.insert(scratch);
        }
    }

}  // namespace cl::jit
