#ifndef CL_JIT_INSTRUCTION_RECONSTRUCTION_H
#define CL_JIT_INSTRUCTION_RECONSTRUCTION_H

#include "jit/compilation_storage.h"
#include "jit/instruction.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace cl::jit
{
    namespace detail
    {
        template <typename DefResolver>
        InstructionId resolve_operand_reference(const DefResolver &resolver,
                                                uint32_t operand_index,
                                                InstructionId definition)
        {
            if constexpr(requires {
                             resolver.resolve(operand_index, definition);
                         })
            {
                return resolver.resolve(operand_index, definition);
            }
            else
            {
                return resolver.resolve(definition);
            }
        }

        template <typename DefResolver> class TypedReferenceResolver
        {
        public:
            TypedReferenceResolver(const CompilationStorage &storage,
                                   const DefResolver &resolver)
                : storage_(&storage), resolver_(&resolver)
            {
            }

            InstructionId resolve(InstructionId def)
            {
                return resolve_operand_reference(*resolver_, operand_index_++,
                                                 def);
            }

            ProgramValueRef resolve(ProgramValueRef def)
            {
                return ProgramValueRef(
                    storage_->instruction(resolve(def.instruction_id())));
            }

            TaggedValueRef resolve(TaggedValueRef def)
            {
                return TaggedValueRef(
                    storage_->instruction(resolve(def.instruction_id())));
            }

            F64Ref resolve(F64Ref def)
            {
                return F64Ref(
                    storage_->instruction(resolve(def.instruction_id())));
            }

            PointerRef resolve(PointerRef def)
            {
                return PointerRef(
                    storage_->instruction(resolve(def.instruction_id())));
            }

            SnapshotRef resolve(SnapshotRef def)
            {
                return SnapshotRef(
                    storage_->instruction(resolve(def.instruction_id())));
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
            auto resolve(RepresentedValueRefRange<Representation> defs)
            {
                using Reference = decltype(defs[size_t{0}]);
                std::vector<Reference> resolved;
                resolved.reserve(defs.size());
                for(size_t index = 0; index < defs.size(); ++index)
                {
                    resolved.push_back(resolve(defs[index]));
                }
                return resolved;
            }

            std::vector<ProgramValueRef> resolve(ProgramValueRefRange defs)
            {
                std::vector<ProgramValueRef> resolved;
                resolved.reserve(defs.size());
                for(size_t index = 0; index < defs.size(); ++index)
                {
                    resolved.emplace_back(storage_->instruction(
                        resolve(defs[index].instruction_id())));
                }
                return resolved;
            }

        private:
            const CompilationStorage *storage_;
            const DefResolver *resolver_;
            uint32_t operand_index_ = 0;
        };
    }  // namespace detail

    enum class InstructionRebuildMode : uint8_t
    {
        ReuseIfUnchanged,
        AlwaysClone,
    };

    // Reconstructs an instruction through its schema-generated typed
    // constructor after resolving every operand definition and first-class
    // BlockEdge attribute. Other attributes are copied unchanged. The original
    // instruction is returned when no reference changes unless AlwaysClone is
    // requested.
    //
    // DefResolver provides:
    //
    //     InstructionId resolve(InstructionId def) const;
    //
    // A resolver that needs to distinguish duplicate references may instead
    // additionally provide:
    //
    //     InstructionId resolve(uint32_t operand_index,
    //                           InstructionId def) const;
    //
    // InstructionFactory provides:
    //
    //     template <typename T, typename... Args>
    //     T make_instruction(Args &&...args);
    template <typename DefResolver, typename InstructionFactory>
    Instruction rebuild_instruction_with_references(
        Instruction &instruction, const CompilationStorage &storage,
        const DefResolver &def_resolver, InstructionFactory &factory,
        InstructionRebuildMode mode = InstructionRebuildMode::ReuseIfUnchanged)
    {
        bool changed = false;
        if(mode == InstructionRebuildMode::ReuseIfUnchanged)
        {
            visit_operand_references(
                instruction, [&](uint32_t operand_index, OperandClass,
                                 ValueRepresentationRequirement,
                                 InstructionId definition_id) {
                    changed |= detail::resolve_operand_reference(
                                   def_resolver, operand_index,
                                   definition_id) != definition_id;
                });
            if(instruction.is_block_terminator())
            {
                for(BlockEdge *edge:
                    TerminatorInstruction(instruction).block_successor_edges())
                {
                    changed |= def_resolver.resolve(edge) != edge;
                }
            }
            if(!changed)
            {
                return instruction;
            }
        }

        detail::TypedReferenceResolver resolver(storage, def_resolver);
        switch(instruction.kind())
        {
#define CL_JIT_IR_LEVELS(...)
#define CL_JIT_RESULT(...)
#define CL_JIT_EFFECT_BOUNDS(...)
#define CL_JIT_DECLARE_RESOLVED_FIXED(name, ...)                               \
    auto resolved_##name = resolver.resolve(typed.name());
#define CL_JIT_DECLARE_RESOLVED_VARIADIC(name, ...)                            \
    auto resolved_##name = resolver.resolve(typed.name());
#define CL_JIT_DECLARE_RESOLVED_PROGRAM_VALUES(name, role)                     \
    auto resolved_##name = resolver.resolve(typed.name());
#define CL_JIT_USE_RESOLVED_FIXED(name, ...) resolved_##name,
#define CL_JIT_USE_RESOLVED_VARIADIC(name, ...) resolved_##name,
#define CL_JIT_USE_RESOLVED_PROGRAM_VALUES(name, role) resolved_##name,
#define CL_JIT_COPY_ATTRIBUTE(name, ...)                                       \
    resolver.resolve_attribute(typed.name()),
#define CL_JIT_RECONSTRUCT_SUBKIND(subkind, family, operands, attributes)      \
    case InstructionKind::subkind:                                             \
        {                                                                      \
            [[maybe_unused]] const subkind##Instruction typed =                \
                instruction.as<subkind##Instruction>();                        \
            operands(CL_JIT_DECLARE_RESOLVED_FIXED,                            \
                     CL_JIT_DECLARE_RESOLVED_VARIADIC,                         \
                     CL_JIT_DECLARE_RESOLVED_PROGRAM_VALUES);                  \
            return factory.template make_instruction<subkind##Instruction>(    \
                operands(CL_JIT_USE_RESOLVED_FIXED,                            \
                         CL_JIT_USE_RESOLVED_VARIADIC,                         \
                         CL_JIT_USE_RESOLVED_PROGRAM_VALUES)                   \
                    attributes(CL_JIT_COPY_ATTRIBUTE)                          \
                        InstructionConstructorEnd{});                          \
        }
#define CL_JIT_INSTRUCTION_FAMILY(name, ir_levels, result, effects, operands,  \
                                  attributes, subkinds)                        \
    subkinds(CL_JIT_RECONSTRUCT_SUBKIND, name, operands, attributes)
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    CL_JIT_RECONSTRUCT_SUBKIND(name, name, operands, attributes)
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_INSTRUCTION_FAMILY
#undef CL_JIT_RECONSTRUCT_SUBKIND
#undef CL_JIT_COPY_ATTRIBUTE
#undef CL_JIT_USE_RESOLVED_PROGRAM_VALUES
#undef CL_JIT_USE_RESOLVED_VARIADIC
#undef CL_JIT_USE_RESOLVED_FIXED
#undef CL_JIT_DECLARE_RESOLVED_PROGRAM_VALUES
#undef CL_JIT_DECLARE_RESOLVED_VARIADIC
#undef CL_JIT_DECLARE_RESOLVED_FIXED
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
        }
        assert(false);
        return instruction;
    }

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_RECONSTRUCTION_H
