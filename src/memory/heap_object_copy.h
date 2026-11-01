#ifndef CL_HEAP_OBJECT_COPY_H
#define CL_HEAP_OBJECT_COPY_H

namespace cl
{
    class HeapObject;
    class HeapSpace;

    // Collector-only byte copying into storage owned by destination. Both
    // arguments are required; source must not be forwarded. This does not
    // establish RC ownership, change generation, publish forwarding, or repair
    // references. The caller owns layout admission and subsequent publication.
    // Never release references or run destruction on an abandoned byte copy.
    //
    // Temporary accepted C++ lifetime exception: retaining Member/MemberHeapPtr
    // wrappers prevent implicit object creation for some current layouts.
    // Replace them and enforce trivial-copyability for admitted layouts at the
    // RC-wrapper cutover; do not add per-layout relocation constructors.
    HeapObject *copy_object_bytes(HeapSpace *destination,
                                  const HeapObject *source);
}  // namespace cl

#endif  // CL_HEAP_OBJECT_COPY_H
