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
        absl::flat_hash_map<InstructionId, InstructionId> normalized_ids;
        normalized_ids.reserve(normalization.size());
        for(const auto &[before, after]: normalization)
        {
            normalized_ids.emplace(before->id(), after->id());
        }

        LocationAssignments::ProgramValueMap program_values;
        program_values.reserve(program_values_.size());
        for(const auto &[id, location]: program_values_)
        {
            auto normalized = normalized_ids.find(id);
            program_values.insert_or_assign(
                normalized == normalized_ids.end() ? id : normalized->second,
                location);
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
