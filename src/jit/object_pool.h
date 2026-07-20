#ifndef CL_JIT_OBJECT_POOL_H
#define CL_JIT_OBJECT_POOL_H

#include "jit/serial.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <type_traits>
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

    template <typename Base> class PolymorphicObjectPool
    {
    public:
        PolymorphicObjectPool() = default;

        PolymorphicObjectPool(const PolymorphicObjectPool &) = delete;
        PolymorphicObjectPool &
        operator=(const PolymorphicObjectPool &) = delete;
        PolymorphicObjectPool(PolymorphicObjectPool &&) = delete;
        PolymorphicObjectPool &operator=(PolymorphicObjectPool &&) = delete;

        template <typename Derived, typename... Args>
        Derived *make(Args &&...args)
        {
            static_assert(std::is_base_of_v<Base, Derived>);
            static_assert(std::has_virtual_destructor_v<Base>);

            TypedSerial<Base> serial(next_serial_++);
            auto object =
                std::make_unique<Derived>(serial, std::forward<Args>(args)...);
            Derived *result = object.get();
            objects_.push_back(std::move(object));
            return result;
        }

    private:
        uint64_t next_serial_ = 0;
        std::deque<std::unique_ptr<Base>> objects_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_OBJECT_POOL_H
