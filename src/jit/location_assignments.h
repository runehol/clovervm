#ifndef CL_JIT_LOCATION_ASSIGNMENTS_H
#define CL_JIT_LOCATION_ASSIGNMENTS_H

#include "jit/graph_rewriter.h"
#include "jit/physical_location.h"

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <utility>

namespace cl::jit
{
    class LocationAssignmentsBuilder;

    class LocationAssignments
    {
    public:
        bool contains(ProgramValueRef value) const
        {
            return program_values_.contains(value.instruction());
        }

        bool contains(const Instruction *instruction,
                      size_t temporary_index) const
        {
            return temporaries_.contains({instruction, temporary_index});
        }

        PhysicalLocation location_for(ProgramValueRef value) const
        {
            return program_values_.at(value.instruction());
        }

        PhysicalLocation location_for(const Instruction *instruction,
                                      size_t temporary_index) const
        {
            return temporaries_.at({instruction, temporary_index});
        }

    private:
        friend class LocationAssignmentsBuilder;

        using ProgramValueMap =
            absl::flat_hash_map<const Instruction *, PhysicalLocation>;
        using TemporaryKey = std::pair<const Instruction *, size_t>;
        using TemporaryMap =
            absl::flat_hash_map<TemporaryKey, PhysicalLocation>;

        LocationAssignments(ProgramValueMap program_values,
                            TemporaryMap temporaries)
            : program_values_(std::move(program_values)),
              temporaries_(std::move(temporaries))
        {
        }

        ProgramValueMap program_values_;
        TemporaryMap temporaries_;
    };

    class LocationAssignmentsBuilder
    {
    public:
        void assign(ProgramValueRef value, PhysicalLocation location)
        {
            program_values_.insert_or_assign(value.instruction(), location);
        }

        void assign(const Instruction *instruction, size_t temporary_index,
                    PhysicalLocation location)
        {
            temporaries_.insert_or_assign({instruction, temporary_index},
                                          location);
        }

        LocationAssignments finalize() &&
        {
            return LocationAssignments(std::move(program_values_),
                                       std::move(temporaries_));
        }
        LocationAssignments
        finalize(const NormalizationRemapping &normalization) &&;

    private:
        LocationAssignments::ProgramValueMap program_values_;
        LocationAssignments::TemporaryMap temporaries_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_LOCATION_ASSIGNMENTS_H
