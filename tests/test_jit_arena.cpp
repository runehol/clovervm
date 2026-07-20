#include "jit/compilation_arena.h"
#include "jit/object_pool.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace cl::jit
{
    namespace
    {
        class DirectTestObject
        {
        public:
            using Serial = TypedSerial<DirectTestObject>;

            DirectTestObject(Serial serial, int value)
                : serial_(serial), value_(value)
            {
            }

            Serial serial() const { return serial_; }
            int value() const { return value_; }

        private:
            Serial serial_;
            int value_;
        };

        class TestInstruction final : public CoreInstruction
        {
        public:
            TestInstruction(Serial serial, int value, bool *destroyed)
                : CoreInstruction(serial), value_(value), destroyed_(destroyed)
            {
            }

            ~TestInstruction() override { *destroyed_ = true; }

            int value() const { return value_; }

        private:
            int value_;
            bool *destroyed_;
        };

        class OtherTestInstruction final : public CoreInstruction
        {
        public:
            explicit OtherTestInstruction(Serial serial)
                : CoreInstruction(serial)
            {
            }
        };
    }  // namespace

    TEST(JitObjectPool, AssignsDenseSerialsAndKeepsAddressesStable)
    {
        ObjectPool<DirectTestObject> pool;
        DirectTestObject *first = pool.make(10);
        DirectTestObject *second = pool.make(20);

        for(int value = 0; value < 1024; ++value)
        {
            pool.make(value);
        }

        EXPECT_EQ(0u, first->serial().value());
        EXPECT_EQ(10, first->value());
        EXPECT_EQ(1u, second->serial().value());
        EXPECT_EQ(20, second->value());
    }

    TEST(JitCompilationArena, UsesOneSerialSequencePerPool)
    {
        bool destroyed = false;
        CompilationArena arena;
        CoreBlock *first_block = arena.make_core_block();
        CoreBlock *second_block = arena.make_core_block();
        TestInstruction *first_instruction =
            arena.make_core_instruction<TestInstruction>(42, &destroyed);
        OtherTestInstruction *second_instruction =
            arena.make_core_instruction<OtherTestInstruction>();

        EXPECT_EQ(0u, first_block->serial().value());
        EXPECT_EQ(1u, second_block->serial().value());
        EXPECT_EQ(0u, first_instruction->serial().value());
        EXPECT_EQ(1u, second_instruction->serial().value());
        EXPECT_EQ(42, first_instruction->value());
        EXPECT_FALSE(destroyed);
    }

    TEST(JitCompilationArena, DestroysPolymorphicObjects)
    {
        bool destroyed = false;
        {
            CompilationArena arena;
            arena.make_core_instruction<TestInstruction>(7, &destroyed);
            EXPECT_FALSE(destroyed);
        }
        EXPECT_TRUE(destroyed);
    }

}  // namespace cl::jit
