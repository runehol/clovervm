#include "jit/location_assignments.h"

#include <utility>

namespace cl::jit
{
    LocationAssignments LocationAssignmentsBuilder::finalize(
        const NormalizationRemapping &normalization) &&
    {
        LocationAssignments::ProgramValueMap program_values;
        program_values.reserve(program_values_.size());
        for(const auto &[id, location]: program_values_)
        {
            auto normalized = normalization.find(id);
            program_values.insert_or_assign(
                normalized == normalization.end() ? id : normalized->second,
                location);
        }

        LocationAssignments::TemporaryMap temporaries;
        temporaries.reserve(temporaries_.size());
        for(const auto &[temporary, location]: temporaries_)
        {
            temporaries.insert_or_assign(
                LocationAssignments::TemporaryKey{
                    normalization.contains(temporary.first)
                        ? normalization.at(temporary.first)
                        : temporary.first,
                    temporary.second},
                location);
        }
        return LocationAssignments(std::move(program_values),
                                   std::move(temporaries));
    }

}  // namespace cl::jit
