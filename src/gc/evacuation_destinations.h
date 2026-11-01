#ifndef CL_GC_EVACUATION_DESTINATIONS_H
#define CL_GC_EVACUATION_DESTINATIONS_H

namespace cl
{
    class HeapObject;
    class HeapSpace;
}  // namespace cl

namespace cl::gc
{
    // Borrowed, distinct spaces owned beyond the collection. Separate CloverVM
    // placement policy chooses the method, not this component or the collector.
    // Sources must be admitted, movable, non-forwarded objects outside the
    // destination mappings. Selected evacuation spaces remain frozen.
    // Copies are unpublished: no forwarding, marking, RC traffic, or repair.
    // The caller owns publication and must never destroy/release abandoned
    // byte copies. The temporary C++ lifetime exception documented by
    // copy_object_bytes still applies. This does not enable runtime movement.
    class EvacuationDestinations
    {
    public:
        EvacuationDestinations(HeapSpace &old, HeapSpace &non_moving) noexcept;

        [[nodiscard]] HeapObject *
        copy_to_old(const HeapObject *source) noexcept;
        [[nodiscard]] HeapObject *
        copy_to_non_moving(const HeapObject *source) noexcept;

    private:
        HeapSpace &old_;
        HeapSpace &non_moving_;
    };
}  // namespace cl::gc

#endif  // CL_GC_EVACUATION_DESTINATIONS_H
