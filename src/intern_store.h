#ifndef CL_INTERN_STORE_H
#define CL_INTERN_STORE_H

#include "heap.h"
#include "value.h"
#include <absl/container/flat_hash_map.h>

namespace cl
{

    class VirtualMachine;

    template <typename BasicType, typename CLType> class InternStore
    {
    public:
        InternStore(GlobalHeap *_intern_heap) : intern_heap(_intern_heap) {}

        Value get_or_create(const BasicType &src)
        {
            auto it = map.find(src);
            if(it != map.end())
                return Value::from_oop(it->second);

            CLType *value = intern_heap->make_global_sized<CLType>(
                CLType::size_for(src), src);
            value->refcount = -1;  // signifying immortality
            map[src] = value;
            return Value::from_oop(value);
        }

    private:
        GlobalHeap *intern_heap;
        absl::flat_hash_map<BasicType, CLType *> map;
    };

}  // namespace cl

#endif  // CL_INTERN_STORE_H
