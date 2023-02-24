#ifndef CL_SLAB_ALLOCATOR_H
#define CL_SLAB_ALLOCATOR_H

#include <cstdlib>
#include "value.h"


namespace cl
{


    class SlabAllocator
    {
    public:
        SlabAllocator(size_t offset, size_t slab_size);
        ~SlabAllocator();


        char *allocate(size_t n_bytes)
        {
            if(curr_ptr + n_bytes > end_ptr)
            {
                return nullptr;
            }
            char *result = curr_ptr;
            n_bytes = (n_bytes + value_ptr_granularity-1)&~(value_ptr_granularity-1);
            curr_ptr += n_bytes;
            return result;
        }

    private:
        char *start_ptr;
        char *curr_ptr;
        char *end_ptr;

    };

}

#endif //CL_SLAB_ALLOCATOR_H
