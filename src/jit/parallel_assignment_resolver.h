#ifndef CL_JIT_PARALLEL_ASSIGNMENT_RESOLVER_H
#define CL_JIT_PARALLEL_ASSIGNMENT_RESOLVER_H

#include "jit/physical_register.h"
#include "runtime/fatal.h"
#include "util/result.h"

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    enum class ParallelAssignmentError : uint8_t
    {
        InsufficientScratchLocations,
    };

    // One assignment in a simultaneous location shuffle. The value initially
    // in source must end in destination; register_class is passed to the
    // caller when the ordering engine needs a cycle scratch location.
    template <typename Location> struct ParallelAssignment
    {
        Location source;
        Location destination;
        RegisterClass register_class;
    };

    class OrderedMoveSource
    {
    public:
        enum class Kind : uint8_t
        {
            OriginalAssignment,
            Move,
        };

        static OrderedMoveSource original_assignment(size_t index)
        {
            return {Kind::OriginalAssignment, index};
        }
        static OrderedMoveSource move(size_t index)
        {
            return {Kind::Move, index};
        }

        Kind kind() const { return kind_; }
        size_t index() const { return index_; }

    private:
        OrderedMoveSource(Kind kind, size_t index) : kind_(kind), index_(index)
        {
        }

        Kind kind_;
        size_t index_;
    };

    template <typename Location> struct OrderedMove
    {
        OrderedMoveSource source;
        Location source_location;
        Location destination;
        RegisterClass register_class;
        int original_assignment_index;
    };

    template <typename Location> struct OrderedParallelAssignment
    {
        std::vector<size_t> aliasing_assignments;
        std::vector<OrderedMove<Location>> moves;
    };

    namespace parallel_assignment_detail
    {
        template <typename Location> struct PendingAssignment
        {
            int original_index;
            OrderedMoveSource source;
            Location source_location;
            Location destination;
            RegisterClass register_class;
            bool pending = true;
            bool queued = false;
        };

        template <typename Location, typename MakeCycleScratch>
        class ParallelAssignmentResolver
        {
        public:
            ParallelAssignmentResolver(
                std::span<const ParallelAssignment<Location>> assignments,
                MakeCycleScratch make_cycle_scratch)
                : make_cycle_scratch_(std::move(make_cycle_scratch))
            {
                pending_.reserve(assignments.size());
                int original_index = 0;
                for(const ParallelAssignment<Location> &assignment: assignments)
                {
                    if(assignment.source == assignment.destination)
                    {
                        result_.aliasing_assignments.push_back(original_index);
                        ++original_index;
                        continue;
                    }
                    if(assignment_by_destination_.contains(
                           assignment.destination))
                    {
                        fatal("parallel assignments have a duplicate "
                              "destination");
                    }

                    size_t pending_index = pending_.size();
                    pending_.push_back(
                        {original_index,
                         OrderedMoveSource::original_assignment(original_index),
                         assignment.source, assignment.destination,
                         assignment.register_class});
                    ++n_pending_sources_[assignment.source];
                    assignments_by_source_[assignment.source].push_back(
                        pending_index);
                    assignment_by_destination_.emplace(assignment.destination,
                                                       pending_index);
                    cycle_candidates_.push_back(pending_index);
                    ++original_index;
                }
                n_pending_ = pending_.size();
                for(size_t index = 0; index < pending_.size(); ++index)
                {
                    enqueue_if_ready(index);
                }
            }

            Result<OrderedParallelAssignment<Location>, ParallelAssignmentError>
            resolve()
            {
                while(n_pending_ != 0)
                {
                    if(!ready_.empty())
                    {
                        size_t index = ready_.front();
                        ready_.pop_front();
                        PendingAssignment<Location> &assignment =
                            pending_[index];
                        assignment.queued = false;
                        result_.moves.push_back(
                            {assignment.source, assignment.source_location,
                             assignment.destination, assignment.register_class,
                             assignment.original_index});
                        complete(index);
                        continue;
                    }

                    size_t cycle = next_cycle();
                    PendingAssignment<Location> &selected = pending_[cycle];
                    std::optional<Location> scratch = make_cycle_scratch_(
                        selected.register_class, result_.moves.size());
                    if(!scratch.has_value())
                    {
                        return Result<OrderedParallelAssignment<Location>,
                                      ParallelAssignmentError>::
                            error(ParallelAssignmentError::
                                      InsufficientScratchLocations);
                    }
                    break_cycle(cycle, *scratch);
                }
                return Result<OrderedParallelAssignment<Location>,
                              ParallelAssignmentError>::ok(std::move(result_));
            }

        private:
            void enqueue_if_ready(size_t index)
            {
                PendingAssignment<Location> &assignment = pending_[index];
                if(!assignment.pending || assignment.queued ||
                   n_pending_sources_.contains(assignment.destination))
                {
                    return;
                }
                assignment.queued = true;
                ready_.push_back(index);
            }

            void source_became_free(Location source)
            {
                auto destination = assignment_by_destination_.find(source);
                if(destination != assignment_by_destination_.end())
                {
                    enqueue_if_ready(destination->second);
                }
            }

            void complete(size_t index)
            {
                PendingAssignment<Location> &assignment = pending_[index];
                if(!assignment.pending)
                {
                    fatal("parallel assignment completed twice");
                }
                assignment.pending = false;
                --n_pending_;

                Location source = assignment.source_location;
                auto count = n_pending_sources_.find(source);
                if(count == n_pending_sources_.end() || count->second <= 0)
                {
                    fatal("invalid parallel-assignment pending-source count");
                }
                if(--count->second != 0)
                {
                    return;
                }
                n_pending_sources_.erase(count);
                assignments_by_source_.erase(source);
                source_became_free(source);
            }

            size_t next_cycle()
            {
                while(!cycle_candidates_.empty())
                {
                    size_t index = cycle_candidates_.front();
                    cycle_candidates_.pop_front();
                    const PendingAssignment<Location> &assignment =
                        pending_[index];
                    if(assignment.pending &&
                       n_pending_sources_.contains(assignment.destination))
                    {
                        return index;
                    }
                }
                fatal("parallel assignments have no ready move or cycle");
            }

            void break_cycle(size_t cycle, Location scratch)
            {
                PendingAssignment<Location> &selected = pending_[cycle];
                size_t scratch_move = result_.moves.size();
                result_.moves.push_back({selected.source,
                                         selected.source_location, scratch,
                                         selected.register_class, -1});
                Location old_source = selected.source_location;

                auto source_users = assignments_by_source_.find(old_source);
                if(source_users == assignments_by_source_.end())
                {
                    fatal("parallel-assignment cycle source has no users");
                }
                std::vector<size_t> users = std::move(source_users->second);
                assignments_by_source_.erase(source_users);

                int32_t moved_sources = 0;
                for(size_t index: users)
                {
                    PendingAssignment<Location> &assignment = pending_[index];
                    if(!assignment.pending ||
                       assignment.source_location != old_source)
                    {
                        continue;
                    }
                    assignment.source = OrderedMoveSource::move(scratch_move);
                    assignment.source_location = scratch;
                    assignments_by_source_[scratch].push_back(index);
                    ++moved_sources;
                }

                auto old_source_count = n_pending_sources_.find(old_source);
                if(old_source_count == n_pending_sources_.end() ||
                   old_source_count->second != moved_sources)
                {
                    fatal("invalid parallel-assignment cycle source count");
                }
                n_pending_sources_.erase(old_source_count);
                n_pending_sources_.emplace(scratch, moved_sources);
                source_became_free(old_source);
            }

            MakeCycleScratch make_cycle_scratch_;
            OrderedParallelAssignment<Location> result_;
            std::vector<PendingAssignment<Location>> pending_;
            size_t n_pending_ = 0;
            absl::flat_hash_map<Location, int32_t> n_pending_sources_;
            absl::flat_hash_map<Location, std::vector<size_t>>
                assignments_by_source_;
            absl::flat_hash_map<Location, size_t> assignment_by_destination_;
            std::deque<size_t> ready_;
            std::deque<size_t> cycle_candidates_;
        };
    }  // namespace parallel_assignment_detail

    // make_cycle_scratch is called only to break a cycle. It receives the
    // value's register class and the index of the preservation move, and must
    // return a location outside the original assignment location set.
    template <typename Location, typename MakeCycleScratch>
    Result<OrderedParallelAssignment<Location>, ParallelAssignmentError>
    order_parallel_assignments(
        std::span<const ParallelAssignment<Location>> assignments,
        MakeCycleScratch make_cycle_scratch)
    {
        return parallel_assignment_detail::ParallelAssignmentResolver(
                   assignments, std::move(make_cycle_scratch))
            .resolve();
    }

}  // namespace cl::jit

#endif  // CL_JIT_PARALLEL_ASSIGNMENT_RESOLVER_H
