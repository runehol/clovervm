#ifndef CL_JIT_SERIAL_H
#define CL_JIT_SERIAL_H

#include <cstdint>

namespace cl::jit
{
    template <typename T> class TypedSerial
    {
    public:
        explicit TypedSerial(uint64_t value) : value_(value) {}

        uint64_t value() const { return value_; }

        bool operator==(TypedSerial other) const
        {
            return value_ == other.value_;
        }

        bool operator!=(TypedSerial other) const { return !(*this == other); }

        bool operator<(TypedSerial other) const
        {
            return value_ < other.value_;
        }

    private:
        uint64_t value_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_SERIAL_H
