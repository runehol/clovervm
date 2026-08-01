#ifndef CL_JIT_CORE_BYTECODE_TRANSLATOR_H
#define CL_JIT_CORE_BYTECODE_TRANSLATOR_H

#include "bytecode/bytecode_decoder.h"
#include "jit/bytecode_state.h"
#include "jit/graph_builder.h"

#include <cstddef>
#include <vector>

namespace cl::jit
{
    class CoreBytecodeTranslator
    {
    public:
        CoreBytecodeTranslator(const CodeObject &code_object,
                               GraphBuilder &builder)
            : code_object_(code_object), builder_(builder),
              decoder_(code_object), state_tracker_(code_object)
        {
        }

        CoreBytecodeTranslator(const CoreBytecodeTranslator &) = delete;
        CoreBytecodeTranslator &
        operator=(const CoreBytecodeTranslator &) = delete;
        CoreBytecodeTranslator(CoreBytecodeTranslator &&) = delete;
        CoreBytecodeTranslator &operator=(CoreBytecodeTranslator &&) = delete;

        ControlFlowGraph *translate();

    private:
        using State = BytecodeState<ProgramValueRef>;

        State make_entry_state(Block *block);
        State make_block_entry_state(Block *block);
        ProgramValueRef emplace_state_parameter(Block *block,
                                                size_t state_position);

        void translate_block(const BytecodeBlock &bytecode_block);
        void translate_sequential_instruction(
            Block *block, const BytecodeInstruction &instruction, State &state);
        void translate_control_instruction(
            Block *block, const BytecodeBlock &bytecode_block,
            const BytecodeInstruction &instruction, State &state);

        void emit_unsupported(Block *block,
                              const BytecodeInstruction &instruction,
                              const State &pre_instruction_state);
        SnapshotRef emit_snapshot(Block *block,
                                  BytecodePCOffset resume_pc_offset,
                                  const State &state);
        std::vector<ProgramValueRef>
        capture_snapshot_values(const State &state) const;

        ProgramValueRef emit_constant(Block *block, Value value);
        BlockEdge *make_state_edge(Block *source, BytecodeBlockId target,
                                   const State &state);

        TaggedValueRef tagged(ProgramValueRef value) const
        {
            return TaggedValueRef(
                builder_.storage()->instruction(value.instruction_id()));
        }

        PointerRef pointer(ProgramValueRef value) const
        {
            return PointerRef(
                builder_.storage()->instruction(value.instruction_id()));
        }

        const CodeObject &code_object_;
        GraphBuilder &builder_;
        BytecodeDecoder decoder_;
        BytecodeStateTracker<ProgramValueRef> state_tracker_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CORE_BYTECODE_TRANSLATOR_H
