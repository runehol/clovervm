#include "jit/graph_builder.h"
#include "jit/location_assignments.h"

#include <gtest/gtest.h>

namespace cl::jit
{
    TEST(JitLocationAssignments, StoresProgramValuesAndTemporaries)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction *move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(move));
        builder.finalize();

        PhysicalRegister x0(RegisterClass::GPR, 0);
        PhysicalRegister x1(RegisterClass::GPR, 1);
        LocationAssignmentsBuilder locations;
        locations.assign(ProgramValueRef(parameter),
                         AllocationLocation::reg(x0));
        locations.assign(move, 0, AllocationLocation::reg(x1));

        LocationAssignments result = std::move(locations).finalize();

        EXPECT_TRUE(result.contains(ProgramValueRef(parameter)));
        EXPECT_EQ(x0, result.location_for(ProgramValueRef(parameter)).reg());
        EXPECT_TRUE(result.contains(move, 0));
        EXPECT_EQ(x1, result.location_for(move, 0).reg());
        EXPECT_FALSE(result.contains(ProgramValueRef(move)));
        EXPECT_FALSE(result.contains(move, 1));
    }

    TEST(JitLocationAssignments, RemapsKeysAfterInstructionNormalization)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction *before = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        MovInstruction *after = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(parameter));
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(after));
        builder.finalize();

        PhysicalRegister x0(RegisterClass::GPR, 0);
        PhysicalRegister x1(RegisterClass::GPR, 1);
        LocationAssignmentsBuilder locations;
        locations.assign(ProgramValueRef(before), AllocationLocation::reg(x0));
        locations.assign(before, 0, AllocationLocation::reg(x1));
        NormalizationRemapping normalization = {{before, after}};

        LocationAssignments result =
            std::move(locations).finalize(normalization);

        EXPECT_FALSE(result.contains(ProgramValueRef(before)));
        EXPECT_TRUE(result.contains(ProgramValueRef(after)));
        EXPECT_EQ(x0, result.location_for(ProgramValueRef(after)).reg());
        EXPECT_FALSE(result.contains(before, 0));
        EXPECT_TRUE(result.contains(after, 0));
        EXPECT_EQ(x1, result.location_for(after, 0).reg());
    }

}  // namespace cl::jit
