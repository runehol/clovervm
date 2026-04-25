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

        template <typename Source> CLType *get_or_create_raw(const Source &src)
        {
            auto it = map.find(src);
            if(it != map.end())
            {
                return it->second;
            }

            CLType *value = intern_heap->make_global_raw<CLType>(src);
            value->refcount = -1;  // signifying immortality
            map.emplace(BasicType(src), value);
            return value;
        }

        template <typename Source>
        TValue<CLType> get_or_create_value(const Source &src)
        {
            return TValue<CLType>::from_oop(get_or_create_raw(src));
        }

    private:
        GlobalHeap *intern_heap;
        absl::flat_hash_map<BasicType, CLType *> map;
    };

}  // namespace cl

#endif  // CL_INTERN_STORE_H
