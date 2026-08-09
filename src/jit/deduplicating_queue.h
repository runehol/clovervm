#ifndef CL_JIT_DEDUPLICATING_QUEUE_H
#define CL_JIT_DEDUPLICATING_QUEUE_H

#include <absl/container/flat_hash_set.h>

#include <cassert>
#include <cstddef>
#include <deque>

namespace cl::jit
{
    template <typename T> class DeduplicatingQueue
    {
    public:
        bool enqueue(const T &value)
        {
            if(!queued_.insert(value).second)
            {
                return false;
            }
            queue_.push_back(value);
            return true;
        }

        T dequeue()
        {
            assert(!queue_.empty());
            T value = queue_.front();
            queue_.pop_front();
            size_t erased = queued_.erase(value);
            assert(erased == 1);
            (void)erased;
            return value;
        }

        bool empty() const { return queue_.empty(); }
        size_t size() const { return queue_.size(); }

    private:
        std::deque<T> queue_;
        absl::flat_hash_set<T> queued_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_DEDUPLICATING_QUEUE_H
