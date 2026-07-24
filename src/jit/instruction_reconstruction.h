#ifndef CL_JIT_INSTRUCTION_RECONSTRUCTION_H
#define CL_JIT_INSTRUCTION_RECONSTRUCTION_H

#include "jit/instruction.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace cl::jit
{
    namespace detail
    {
        template <typename DefResolver> class TypedReferenceResolver
        {
        public:
            explicit TypedReferenceResolver(const DefResolver &resolver)
                : resolver_(&resolver)
            {
            }

            Instruction *resolve(Instruction *def) const
            {
                return resolver_->resolve(def);
            }

            TaggedValueRef resolve(TaggedValueRef def) const
            {
                return TaggedValueRef(resolve(def.instruction()));
            }

            F64Ref resolve(F64Ref def) const
            {
                return F64Ref(resolve(def.instruction()));
            }

            SnapshotRef resolve(SnapshotRef def) const
            {
                return SnapshotRef(resolve(def.instruction()));
            }

            template <typename T> T resolve_attribute(T attribute) const
            {
                return attribute;
            }

            BlockEdge *resolve_attribute(BlockEdge *edge) const
            {
                return resolver_->resolve(edge);
            }

            template <ValueRepresentation Representation>
            std::vector<RepresentedValueRef<Representation>>
            resolve(ProgramValueRefRange<Representation> defs) const
            {
                std::vector<RepresentedValueRef<Representation>> resolved;
                resolved.reserve(defs.size());
                for(size_t index = 0; index < defs.size(); ++index)
                {
                    resolved.push_back(resolve(defs[index]));
                }
                return resolved;
            }

            std::vector<ProgramValueRef>
            resolve(SnapshotValueRefRange defs) const
            {
                std::vector<ProgramValueRef> resolved;
                resolved.reserve(defs.size());
                for(size_t index = 0; index < defs.size(); ++index)
                {
                    resolved.emplace_back(resolve(defs[index].instruction()));
                }
                return resolved;
            }

        private:
            const DefResolver *resolver_;
        };
    }  // namespace detail

    // Reconstructs an instruction through its schema-generated typed
    // constructor after resolving every operand definition and first-class
    // BlockEdge attribute. Other attributes are copied unchanged. The original
    // instruction is returned when no reference changes.
    //
    // DefResolver provides:
    //
    //     Instruction *resolve(Instruction *def) const;
    //
    // InstructionFactory provides:
    //
    //     template <typename T, typename... Args>
    //     T *make_instruction(Args &&...args);
    template <typename DefResolver, typename InstructionFactory>
    Instruction *
    rebuild_instruction_with_references(Instruction &instruction,
                                        const DefResolver &def_resolver,
                                        InstructionFactory &factory)
    {
        bool changed = false;
        visit_operand_references(
            instruction,
            [&](uint32_t, OperandClass, ValueRepresentation, Instruction *def) {
                changed |= def_resolver.resolve(def) != def;
            });
        if(instruction.is_block_terminator())
        {
            for(BlockEdge *edge:
                TerminatorInstruction(&instruction).block_successor_edges())
            {
                changed |= def_resolver.resolve(edge) != edge;
            }
        }
        if(!changed)
        {
            return &instruction;
        }

        detail::TypedReferenceResolver resolver(def_resolver);
        switch(instruction.kind())
        {
#define CL_JIT_IR_LEVELS(...)
#define CL_JIT_RESULT(...)
#define CL_JIT_EFFECT_BOUNDS(...)
#define CL_JIT_RESOLVE_FIXED(name, ...) resolver.resolve(typed.name()),
#define CL_JIT_RESOLVE_VARIADIC(name, ...) resolver.resolve(typed.name()),
#define CL_JIT_RESOLVE_SNAPSHOT_VALUES(name) resolver.resolve(typed.name()),
#define CL_JIT_COPY_ATTRIBUTE(name, ...)                                       \
    resolver.resolve_attribute(typed.name()),
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        {                                                                      \
            [[maybe_unused]] const name##Instruction &typed =                  \
                *instruction.as<name##Instruction>();                          \
            return factory.template make_instruction<name##Instruction>(       \
                operands(CL_JIT_RESOLVE_FIXED, CL_JIT_RESOLVE_VARIADIC,        \
                         CL_JIT_RESOLVE_SNAPSHOT_VALUES)                       \
                    attributes(CL_JIT_COPY_ATTRIBUTE)                          \
                        InstructionConstructorEnd{});                          \
        }
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_COPY_ATTRIBUTE
#undef CL_JIT_RESOLVE_SNAPSHOT_VALUES
#undef CL_JIT_RESOLVE_VARIADIC
#undef CL_JIT_RESOLVE_FIXED
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
        }
        assert(false);
        return nullptr;
    }

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_RECONSTRUCTION_H
