#include "jit/location_assignments.h"

#include <utility>

namespace cl::jit
{
    namespace
    {
        InstructionId
        resolve_normalized_id(InstructionId id,
                              const NormalizationRemapping &normalization)
        {
            for(auto found = normalization.find(id);
                found != normalization.end(); found = normalization.find(id))
            {
                id = found->second;
            }
            return id;
        }
    }  // namespace

    LocationAssignments LocationAssignmentsBuilder::finalize(
        const NormalizationRemapping &normalization) &&
    {
        LocationAssignments::ProgramValueMap program_values;
        program_values.reserve(program_values_.size());
        for(const auto &[id, location]: program_values_)
        {
            program_values.insert_or_assign(
                resolve_normalized_id(id, normalization), location);
        }

        LocationAssignments::TemporaryMap temporaries;
        temporaries.reserve(temporaries_.size());
        for(const auto &[temporary, location]: temporaries_)
        {
            temporaries.insert_or_assign(
                LocationAssignments::TemporaryKey{
                    resolve_normalized_id(temporary.first, normalization),
                    temporary.second},
                location);
        }
        return LocationAssignments(std::move(program_values),
                                   std::move(temporaries));
    }

}  // namespace cl::jit
