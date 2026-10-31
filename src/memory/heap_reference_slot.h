#ifndef CL_HEAP_REFERENCE_SLOT_H
#define CL_HEAP_REFERENCE_SLOT_H

#include "gc/weak_repair.h"
#include "object_model/heap_object.h"
#include "object_model/value_tags.h"

#include <absl/functional/function_ref.h>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cl
{
    // Borrowed view of one initialized reference-capable cell. Tagged Values
    // and raw heap pointers use the same pointer bits. Byte access also handles
    // cells inside Member wrappers without invoking their ownership operations.
    class HeapReferenceSlot
    {
    public:
        explicit HeapReferenceSlot(void *address)
            : address_(static_cast<std::byte *>(address))
        {
            assert(address_ != nullptr);
        }

        HeapObject *load_pointer() const
        {
            uint64_t bits;
            std::memcpy(&bits, address_, sizeof(bits));
            switch(bits & value_ptr_mask)
            {
                case 0:
                    return nullptr;
                case value_refcounted_ptr_tag:
                case value_interned_ptr_tag:
                    return std::bit_cast<HeapObject *>(bits);
            }
            assert(false && "invalid reference cell encoding");
            __builtin_unreachable();
        }

        // Collector-only relocation: preserves the logical reference and its
        // storage class, without refcounting, barriers, allocation or
        // safepoints.
        void rewrite_pointer(HeapObject *replacement) const
        {
            HeapObject *current = load_pointer();
            assert(current != nullptr);
            assert(replacement != nullptr);
            assert((reinterpret_cast<uintptr_t>(current) & value_ptr_mask) ==
                   (reinterpret_cast<uintptr_t>(replacement) & value_ptr_mask));
            (void)current;
            std::memcpy(address_, &replacement, sizeof(replacement));
        }

    private:
        std::byte *address_;
    };

    static_assert(sizeof(HeapObject *) == sizeof(uint64_t));

    // Borrowed, synchronous visit. The object and its slot storage must remain
    // valid: no managed allocation, safepoints, Python execution, or container
    // mutation during enumeration. Visiting does not transfer ownership.
    using StrongSlotVisitor = absl::FunctionRef<void(HeapReferenceSlot)>;

    using WeakResolution = gc::WeakResolution<HeapObject>;
    using WeakResolver = gc::WeakResolver<HeapObject>;
    using WeakRepairUnit = gc::WeakRepairUnit<HeapObject>;

    // Borrowed enumeration, not transitive tracing. Unit storage must be in
    // its final destination or stable storage and remain valid through late
    // repair; enumerating must not resize tables or reenter the VM. A reset
    // must not invalidate another unit. Resolvers preserve type/storage class,
    // never dereference dead targets, and never make weak edges strong.
    using WeakUnitVisitor = absl::FunctionRef<void(WeakRepairUnit)>;

}  // namespace cl

#endif  // CL_HEAP_REFERENCE_SLOT_H
