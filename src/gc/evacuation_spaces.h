#ifndef CL_GC_EVACUATION_SPACES_H
#define CL_GC_EVACUATION_SPACES_H

#include "object_model/heap_object.h"

#include <cstdint>
#include <span>
#include <vector>

namespace cl
{
    class HeapSpace;
}

namespace cl::gc
{
    struct EvacuationSpace
    {
        const HeapSpace *space;
        // Young or Old. Old includes OldRemembered object headers.
        HeapGenerationState generation;
    };

    // Collection-scoped containment index for selected evacuation spaces, not
    // roots. Stop allocation in selected spaces before construction and keep
    // their mappings alive throughout use. Fresh destination spaces must not be
    // selected.
    class EvacuationSpaces
    {
    public:
        explicit EvacuationSpaces(
            std::span<const EvacuationSpace> sources) noexcept;

        // Requires a valid, non-null object pointer, not an arbitrary address.
        // Membership is determined by mapping ranges. Non-moving or
        // inconsistent-generation headers inside those spaces are invariant
        // failures. Forwarded objects remain contained.
        bool contains(const HeapObject *object) const noexcept;

    private:
        struct SpaceRange
        {
            uintptr_t begin;
            uintptr_t end;
            HeapGenerationState generation;
        };

        std::vector<SpaceRange> ranges_;
    };
}  // namespace cl::gc

#endif  // CL_GC_EVACUATION_SPACES_H
