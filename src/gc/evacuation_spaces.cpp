#include "gc/evacuation_spaces.h"

#include "memory/heap_space.h"
#include "runtime/fatal.h"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace cl::gc
{
    EvacuationSpaces::EvacuationSpaces(
        std::span<const EvacuationSpace> sources) noexcept
    {
        for(const EvacuationSpace &source: sources)
        {
            if(source.space == nullptr)
            {
                fatal("evacuation space must not be null");
            }
            if(source.generation != HeapGenerationState::Young &&
               source.generation != HeapGenerationState::Old)
            {
                fatal("evacuation space generation must be Young or Old");
            }
            source.space->visit_memory_ranges(
                [&](uintptr_t begin, uintptr_t end) {
                    ranges_.push_back({begin, end, source.generation});
                });
        }
        std::sort(ranges_.begin(), ranges_.end(),
                  [](const SpaceRange &left, const SpaceRange &right) {
                      return left.begin < right.begin;
                  });
        for(size_t index = 1; index < ranges_.size(); ++index)
        {
            if(ranges_[index - 1].end > ranges_[index].begin)
            {
                fatal("evacuation space mappings overlap");
            }
        }
    }

    bool EvacuationSpaces::contains(const HeapObject *object) const noexcept
    {
        assert(object != nullptr);
        uintptr_t address = reinterpret_cast<uintptr_t>(object);
        auto range =
            std::upper_bound(ranges_.begin(), ranges_.end(), address,
                             [](uintptr_t value, const SpaceRange &candidate) {
                                 return value < candidate.begin;
                             });
        if(range == ranges_.begin())
        {
            return false;
        }
        --range;
        if(address >= range->end)
        {
            return false;
        }
        if(object->is_non_moving())
        {
            fatal("non-moving object in evacuation space storage");
        }
        HeapGenerationState generation = object->generation_state();
        bool matches =
            range->generation == HeapGenerationState::Young
                ? generation == HeapGenerationState::Young
                : generation == HeapGenerationState::Old ||
                      generation == HeapGenerationState::OldRemembered;
        if(!matches)
        {
            fatal("object generation does not match evacuation space storage");
        }
        return true;
    }
}  // namespace cl::gc
