#ifndef CL_SAFEPOINT_RECLAMATION_H
#define CL_SAFEPOINT_RECLAMATION_H

#include "heap_object.h"
#include "value.h"

#include <absl/container/flat_hash_set.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cl
{
    class ThreadState;

    inline bool value_has_refcounted_pointer_shape(Value value)
    {
        return (static_cast<uint64_t>(value.as.integer) & value_ptr_mask) ==
               value_refcounted_ptr_tag;
    }

    class SafepointRootSet
    {
    public:
        void add_conservative_value(Value value)
        {
            if(value_has_refcounted_pointer_shape(value))
            {
                roots.insert(value.as.ptr);
            }
        }

        bool contains(HeapObject *object) const
        {
            return roots.contains(object);
        }

        size_t size() const { return roots.size(); }

    private:
        absl::flat_hash_set<HeapObject *> roots;
    };

    using ThreadStateList = std::vector<std::unique_ptr<ThreadState>>;

    void collect_safepoint_roots_from_thread(SafepointRootSet &roots,
                                             const ThreadState &thread);
    SafepointRootSet
    collect_safepoint_roots_from_threads(const ThreadStateList &threads);
    void process_zero_count_table_for_safepoint(ThreadState &thread,
                                                const SafepointRootSet &roots);
    void run_safepoint_reclamation(const ThreadStateList &threads);

}  // namespace cl

#endif  // CL_SAFEPOINT_RECLAMATION_H
