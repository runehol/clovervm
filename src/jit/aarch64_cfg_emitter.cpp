#include "jit/aarch64_cfg_emitter.h"

#include "jit/aarch64_assembler.h"
#include "jit/control_flow_graph.h"
#include "jit/instruction.h"

#include <cassert>
#include <utility>

namespace cl::jit
{
    namespace
    {
        XRegister assigned_register(ProgramValueRef value)
        {
            (void)value;
            return XRegister(0);
        }

        Result<CodeAllocation, JitCodeError>
        generate_allocation(const ControlFlowGraph &graph, CodeCache &cache,
                            AArch64ValuePoolMode pool_mode)
        {
            AArch64MacroAssembler assembler(pool_mode);
            generate_aarch64_assembly(graph, assembler);
            return assembler.emitter().finalize(cache);
        }
    }  // namespace

    void generate_aarch64_assembly(const ControlFlowGraph &graph,
                                   AArch64MacroAssembler &assembler)
    {
        assert(graph.is_published());
        assert(graph.blocks().size() == 1);

        const Block *entry = graph.entry_block();
        assert(entry != nullptr);
        assert(graph.blocks()[0] == entry);
        assert(entry->predecessor_edges().empty());
        assert(entry->parameters().size() <= 1);
        if(!entry->parameters().empty())
        {
            assert(entry->parameters()[0]->kind() ==
                   InstructionKind::Parameter);
        }

        for(Instruction *instruction: entry->instructions())
        {
            // clang-format off
            CL_JIT_INSTRUCTION_SWITCH(*instruction)
            {
                case CL_JIT_INSTRUCTION_CASE(ConstInstruction,
                                             constant_instruction)
                {
                    Value constant = constant_instruction.constant();
                    XRegister destination =
                        assigned_register(ProgramValueRef(instruction));
                    if(constant.is_inline())
                    {
                        assembler.mov(
                            destination,
                            static_cast<uint64_t>(constant.as.integer));
                    }
                    else
                    {
                        assembler.ldr(destination, constant);
                    }
                    break;
                }

                case CL_JIT_INSTRUCTION_CASE(ReturnInstruction,
                                             return_instruction)
                {
                    (void)assigned_register(
                        return_instruction.return_value());
                    assembler.emit_ret();
                    break;
                }

                default:
                    assert(false);
            }
            // clang-format on
        }
    }

    Result<JitCodeObject *, JitCodeError>
    emit_aarch64_from_cfg(const ControlFlowGraph &graph, CodeCache &cache)
    {
        Result<CodeAllocation, JitCodeError> near = generate_allocation(
            graph, cache, AArch64ValuePoolMode::NearLiteral);
        if(near)
        {
            return cache.publish(std::move(near).value());
        }
        if(near.error() != JitCodeError::PoolOutOfRange)
        {
            return Result<JitCodeObject *, JitCodeError>::error(near.error());
        }

        Result<CodeAllocation, JitCodeError> far = generate_allocation(
            graph, cache, AArch64ValuePoolMode::FarPageRelative);
        if(!far)
        {
            return Result<JitCodeObject *, JitCodeError>::error(far.error());
        }
        return cache.publish(std::move(far).value());
    }

}  // namespace cl::jit
