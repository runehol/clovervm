#ifndef CL_RANDOM_ENGINE_H
#define CL_RANDOM_ENGINE_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace cl
{
    class ModuleObject;
    class ThreadState;
    class VirtualMachine;

    struct MT19937
    {
        static constexpr size_t word_count = 624;
        std::array<uint32_t, word_count> words;
        uint32_t index = word_count;

        void seed(std::span<const uint32_t> key);
        uint32_t next_word();
        uint32_t next_bits(unsigned width);
    };

    class RandomEngine : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::RandomEngine;
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;

        RandomEngine(ClassObject *cls, const MT19937 &state)
            : Object(cls, native_layout), state(state)
        {
        }

        MT19937 state;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(RandomEngine, Object, 0);
        CL_DECLARE_STATIC_OBJECT_SIZE(RandomEngine);
    };

    static_assert(std::is_trivially_destructible_v<RandomEngine>);

    BuiltinClassDefinition make_random_engine_class(VirtualMachine *vm);
    [[nodiscard]] Value install_random_engine_functions(ThreadState *thread,
                                                        ModuleObject *module);
}  // namespace cl

#endif  // CL_RANDOM_ENGINE_H
