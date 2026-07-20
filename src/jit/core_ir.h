#ifndef CL_JIT_CORE_IR_H
#define CL_JIT_CORE_IR_H

#include "jit/serial.h"

namespace cl::jit
{
    class CoreBlock
    {
    public:
        using Serial = TypedSerial<CoreBlock>;

        explicit CoreBlock(Serial serial) : serial_(serial) {}

        Serial serial() const { return serial_; }

    private:
        Serial serial_;
    };

    class CoreInstruction
    {
    public:
        using Serial = TypedSerial<CoreInstruction>;

        virtual ~CoreInstruction() = default;

        Serial serial() const { return serial_; }

    protected:
        explicit CoreInstruction(Serial serial) : serial_(serial) {}

    private:
        Serial serial_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CORE_IR_H
