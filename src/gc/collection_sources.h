#ifndef CL_GC_COLLECTION_SOURCES_H
#define CL_GC_COLLECTION_SOURCES_H

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
    struct CollectionSourceSpace
    {
        const HeapSpace *space;
        // Young or Old. Old includes OldRemembered object headers.
        HeapGenerationState generation;
    };

    // Collection-scoped snapshot of moving source mappings, not owning roots.
    // Stop source allocation before construction and keep all source mappings
    // alive throughout use. Fresh destination spaces must not be selected.
    class CollectionSources
    {
    public:
        explicit CollectionSources(
            std::span<const CollectionSourceSpace> sources) noexcept;

        // Requires a valid, non-null object pointer, not an arbitrary address.
        // Inside selected storage, non-moving or inconsistent-generation
        // headers are invariant failures. Forwarded sources remain
        // classifiable.
        bool is_collected_source(const HeapObject *object) const noexcept;

    private:
        struct SourceRange
        {
            uintptr_t begin;
            uintptr_t end;
            HeapGenerationState generation;
        };

        std::vector<SourceRange> ranges_;
    };
}  // namespace cl::gc

#endif  // CL_GC_COLLECTION_SOURCES_H
