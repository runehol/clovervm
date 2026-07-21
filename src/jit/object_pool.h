#ifndef CL_JIT_OBJECT_POOL_H
#define CL_JIT_OBJECT_POOL_H

#include "jit/serial.h"

#include <cstdint>
#include <deque>
#include <utility>

namespace cl::jit
{
    template <typename T> class ObjectPool
    {
    public:
        ObjectPool() = default;

        ObjectPool(const ObjectPool &) = delete;
        ObjectPool &operator=(const ObjectPool &) = delete;
        ObjectPool(ObjectPool &&) = delete;
        ObjectPool &operator=(ObjectPool &&) = delete;

        template <typename... Args> T *make(Args &&...args)
        {
            TypedSerial<T> serial(next_serial_++);
            objects_.emplace_back(serial, std::forward<Args>(args)...);
            return &objects_.back();
        }

    private:
        uint64_t next_serial_ = 0;
        std::deque<T> objects_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_OBJECT_POOL_H
