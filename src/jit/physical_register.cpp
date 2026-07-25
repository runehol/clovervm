#include "jit/physical_register.h"

#include "runtime/fatal.h"

namespace cl::jit
{
    RegisterClassDefinition::RegisterClassDefinition(
        RegisterClass register_class,
        std::span<const PhysicalRegister> allocation_order,
        std::optional<PhysicalRegister> scratch_register)
        : register_class_(register_class),
          allocation_order_(allocation_order.begin(), allocation_order.end()),
          scratch_register_(scratch_register)
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

        if(scratch_register_.has_value())
        {
            if(scratch_register_->register_class() != register_class)
            {
                fatal("JIT scratch register has the wrong register class");
            }
            if(members_.contains(*scratch_register_))
            {
                fatal("JIT scratch register is also allocatable");
            }
        }
    }

}  // namespace cl::jit
