#ifndef CL_INTERN_STORE_H
#define CL_INTERN_STORE_H

#include <absl/container/flat_hash_map.h>
#include "value.h"
#include "heap.h"
namespace cl
{

    class VirtualMachine;

    template<typename BasicType, typename CLType>
    class InternStore
    {
    public:
        InternStore(GlobalHeap *_intern_heap)
            : intern_heap(_intern_heap)
        {}

        Value get_or_create(const BasicType &src)
        {
            auto it = map.find(src);
            if(it != map.end()) return Value::from_oop(it->second);

            size_t sz = CLType::size_for(src);
            void *mem = intern_heap->allocate_global(sz);
            CLType *value = new(mem)CLType(src);
            value->refcount = -1; //signifying immortality
            map[src] = value;
            return Value::from_oop(value);
        }

    private:
        GlobalHeap *intern_heap;
        absl::flat_hash_map<BasicType, CLType*> map;
    };



}


#endif //CL_INTERN_STORE_H
