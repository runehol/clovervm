#include "jit/location_assignments.h"

#include <utility>

namespace cl::jit
{
    namespace
    {
        const Instruction *
        normalized_instruction(const NormalizationRemapping &normalization,
                               const Instruction *instruction)
        {
            auto found = normalization.find(instruction);
            if(found == normalization.end())
            {
                return instruction;
            }
            return found->second;
        }
    }  // namespace

    LocationAssignments LocationAssignmentsBuilder::finalize(
        const NormalizationRemapping &normalization) &&
    {
        LocationAssignments::ProgramValueMap program_values;
        program_values.reserve(program_values_.size());
        for(const auto &[instruction, location]: program_values_)
        {
            const Instruction *normalized =
                normalized_instruction(normalization, instruction);
            program_values.insert_or_assign(normalized, location);
        }

        LocationAssignments::TemporaryMap temporaries;
        temporaries.reserve(temporaries_.size());
        for(const auto &[temporary, location]: temporaries_)
        {
            temporaries.insert_or_assign(
                LocationAssignments::TemporaryKey{
                    normalized_instruction(normalization, temporary.first),
                    temporary.second},
                location);
        }
        return LocationAssignments(std::move(program_values),
                                   std::move(temporaries));
    }

}  // namespace cl::jit
