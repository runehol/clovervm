#include "gc/evacuation_destinations.h"

#include "memory/heap_object_copy.h"
#include "object_model/heap_object.h"
#include "runtime/fatal.h"

#include <cassert>

namespace cl::gc
{
    EvacuationDestinations::EvacuationDestinations(
        HeapSpace &old, HeapSpace &non_moving) noexcept
        : old_(old), non_moving_(non_moving)
    {
        if(&old == &non_moving)
        {
            fatal("evacuation destinations must use distinct spaces");
        }
    }

    HeapObject *
    EvacuationDestinations::copy_to_old(const HeapObject *source) noexcept
    {
        assert(source != nullptr && !source->is_non_moving());
        HeapObject *copy = copy_object_bytes(&old_, source);
        copy->set_generation_state(HeapGenerationState::Old);
        return copy;
    }

    HeapObject *EvacuationDestinations::copy_to_non_moving(
        const HeapObject *source) noexcept
    {
        assert(source != nullptr && !source->is_non_moving());
        HeapObject *copy = copy_object_bytes(&non_moving_, source);
        copy->set_generation_state(HeapGenerationState::Old);
        copy->make_non_moving();
        return copy;
    }
}  // namespace cl::gc
