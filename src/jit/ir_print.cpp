#include "jit/ir_print.h"

#include "jit/compilation_storage.h"
#include "jit/control_flow_graph.h"
#include "jit/instruction.h"

#include <fmt/format.h>

#include <bit>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cl::jit
{
    namespace
    {
        class IRPrintState
        {
        public:
            explicit IRPrintState(const ControlFlowGraph &graph)
            {
                assert(graph.is_published());
                for(size_t index = 0; index < graph.blocks().size(); ++index)
                {
                    const Block *block = graph.blocks()[index];
                    assert(block != nullptr);
                    blocks_.emplace(block, index);
                    for(Instruction parameter: block->parameters())
                    {
                        add_result(parameter);
                    }
                    for(Instruction instruction: block->instructions())
                    {
                        add_result(instruction);
                    }
                }
            }

            size_t block_id(const Block *block) const
            {
                auto found = blocks_.find(block);
                assert(found != blocks_.end());
                return found->second;
            }

            size_t result_id(InstructionId instruction) const
            {
                auto found = results_.find(instruction.value());
                assert(found != results_.end());
                return found->second;
            }

            size_t heap_value_id(Value value)
            {
                assert(value.is_ptr());
                uintptr_t bits = static_cast<uintptr_t>(value.as.integer);
                return id_for(heap_values_, bits);
            }

            size_t shape_id(const Shape *shape)
            {
                return id_for(shapes_, shape);
            }

            size_t validity_id(const ValidityCell *validity)
            {
                return id_for(validities_, validity);
            }

            size_t shape_key_id(ShapeKey key)
            {
                uintptr_t bits = std::bit_cast<uintptr_t>(key);
                return id_for(shape_keys_, bits);
            }

        private:
            void add_result(Instruction instruction)
            {
                if(instruction.result_class() != ResultClass::None)
                {
                    results_.emplace(instruction.id().value(), results_.size());
                }
            }

            template <typename Key>
            static size_t id_for(std::unordered_map<Key, size_t> &ids, Key key)
            {
                auto [position, inserted] = ids.emplace(key, ids.size());
                (void)inserted;
                return position->second;
            }

            std::unordered_map<const Block *, size_t> blocks_;
            std::unordered_map<uint32_t, size_t> results_;
            std::unordered_map<uintptr_t, size_t> heap_values_;
            std::unordered_map<const Shape *, size_t> shapes_;
            std::unordered_map<const ValidityCell *, size_t> validities_;
            std::unordered_map<uintptr_t, size_t> shape_keys_;
        };

        std::string instruction_mnemonic(std::string_view class_name)
        {
            if(class_name == "ConditionalBranch")
            {
                return "cond_br";
            }
            if(class_name == "UnconditionalBranch")
            {
                return "br";
            }

            std::string result;
            result.reserve(class_name.size() + 4);
            for(size_t index = 0; index < class_name.size(); ++index)
            {
                char ch = class_name[index];
                bool uppercase =
                    std::isupper(static_cast<unsigned char>(ch)) != 0;
                bool previous_lower =
                    index > 0 && std::islower(static_cast<unsigned char>(
                                     class_name[index - 1])) != 0;
                bool next_lower = index + 1 < class_name.size() &&
                                  std::islower(static_cast<unsigned char>(
                                      class_name[index + 1])) != 0;
                bool previous_upper =
                    index > 0 && std::isupper(static_cast<unsigned char>(
                                     class_name[index - 1])) != 0;
                if(uppercase && index > 0 &&
                   (previous_lower || (previous_upper && next_lower)))
                {
                    result.push_back('_');
                }
                result.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch))));
            }
            return result;
        }

        class OperationPrinter
        {
        public:
            OperationPrinter(fmt::memory_buffer &out, IRPrintState &state,
                             const Instruction &instruction,
                             std::string_view class_name)
                : out_(out), state_(state)
            {
                if(instruction.result_class() != ResultClass::None)
                {
                    print_result_reference(instruction);
                    write(" = ");
                }
                write(instruction_mnemonic(class_name));
            }

            template <typename Ref> void fixed_operand(Ref reference)
            {
                operand_separator();
                print_result_reference(reference.instruction_id());
            }

            template <typename Range> void variadic_operand(const Range &range)
            {
                operand_separator();
                write("[");
                for(size_t index = 0; index < range.size(); ++index)
                {
                    if(index != 0)
                    {
                        write(", ");
                    }
                    print_result_reference(range[index].instruction_id());
                }
                write("]");
            }

            void attribute_Shape(std::string_view name, Shape *shape)
            {
                attribute_separator(name);
                format("<shape{}>", state_.shape_id(shape));
            }

            void attribute_ValidityCell(std::string_view name,
                                        ValidityCell *validity)
            {
                attribute_separator(name);
                format("<validity{}>", state_.validity_id(validity));
            }

            void attribute_ShapeKey(std::string_view name, ShapeKey key)
            {
                attribute_separator(name);
                format("<shape_key{}>", state_.shape_key_id(key));
            }

            void attribute_ValueConstant(std::string_view name, Value value)
            {
                attribute_separator(name);
                print_value(value);
            }

            void attribute_BytecodePC(std::string_view name, BytecodePC pc)
            {
                attribute_separator(name);
                format("{}", pc);
            }

            void attribute_SideExitId(std::string_view name,
                                      SideExitId side_exit)
            {
                attribute_separator(name);
                format("s{}", side_exit.value());
            }

            void attribute_BlockEdge(std::string_view name, BlockEdge *edge)
            {
                assert(edge != nullptr);
                attribute_separator(name);
                format("bb{}", state_.block_id(edge->target()));
                if(!edge->arguments().empty())
                {
                    write("(");
                    for(size_t index = 0; index < edge->arguments().size();
                        ++index)
                    {
                        if(index != 0)
                        {
                            write(", ");
                        }
                        print_result_reference(
                            edge->arguments()[index].instruction_id());
                    }
                    write(")");
                }
            }

            void finish()
            {
                if(has_attributes_)
                {
                    write("}");
                }
            }

        private:
            void operand_separator()
            {
                write(has_operands_ ? ", " : " ");
                has_operands_ = true;
            }

            void attribute_separator(std::string_view name)
            {
                if(!has_attributes_)
                {
                    write(" {");
                    has_attributes_ = true;
                }
                else
                {
                    write(", ");
                }
                write(name);
                write(" = ");
            }

            void print_result_reference(Instruction instruction)
            {
                print_result_reference(instruction.id());
            }

            void print_result_reference(InstructionId instruction)
            {
                format("%{}", state_.result_id(instruction));
            }

            void print_value(Value value)
            {
                if(value == Value::None())
                {
                    write("none");
                }
                else if(value == Value::True())
                {
                    write("true");
                }
                else if(value == Value::False())
                {
                    write("false");
                }
                else if(value == Value::NotImplemented())
                {
                    write("not_implemented");
                }
                else if(value == Value::Ellipsis())
                {
                    write("ellipsis");
                }
                else if(value.is_not_present())
                {
                    write("not_present");
                }
                else if(value.is_exception_marker())
                {
                    write("exception");
                }
                else if(value.is_smi())
                {
                    format("{}", value.get_smi());
                }
                else
                {
                    format("<value{}>", state_.heap_value_id(value));
                }
            }

            void write(std::string_view text)
            {
                fmt::format_to(std::back_inserter(out_), "{}", text);
            }

            template <typename... Args>
            void format(fmt::format_string<Args...> pattern, Args &&...args)
            {
                fmt::format_to(std::back_inserter(out_), pattern,
                               std::forward<Args>(args)...);
            }

            fmt::memory_buffer &out_;
            IRPrintState &state_;
            bool has_operands_ = false;
            bool has_attributes_ = false;
        };

        void print_instruction(fmt::memory_buffer &out, IRPrintState &state,
                               const Instruction &instruction)
        {
            switch(instruction.kind())
            {
#define CL_IR_PRINT_FIXED(name, ...) printer.fixed_operand(concrete.name());
#define CL_IR_PRINT_VARIADIC(name, ...)                                        \
    printer.variadic_operand(concrete.name());
#define CL_IR_PRINT_PROGRAM_VALUES(name)                                       \
    printer.variadic_operand(concrete.name());
#define CL_IR_PRINT_ATTRIBUTE(name, attribute_class)                           \
    printer.attribute_##attribute_class(#name, concrete.name());
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        {                                                                      \
            const name##Instruction &concrete =                                \
                instruction.as<name##Instruction>();                           \
            (void)concrete;                                                    \
            OperationPrinter printer(out, state, instruction, #name);          \
            operands(CL_IR_PRINT_FIXED, CL_IR_PRINT_VARIADIC,                  \
                     CL_IR_PRINT_PROGRAM_VALUES)                               \
                attributes(CL_IR_PRINT_ATTRIBUTE) printer.finish();            \
            return;                                                            \
        }
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_IR_PRINT_ATTRIBUTE
#undef CL_IR_PRINT_PROGRAM_VALUES
#undef CL_IR_PRINT_VARIADIC
#undef CL_IR_PRINT_FIXED
            }
            assert(false);
        }

        void print_parameter(fmt::memory_buffer &out, IRPrintState &state,
                             const Instruction &parameter)
        {
            fmt::format_to(std::back_inserter(out), "%{}",
                           state.result_id(parameter.id()));
            switch(parameter.value_representation())
            {
                case ValueRepresentation::TaggedValue:
                    return;
                case ValueRepresentation::F64:
                    fmt::format_to(std::back_inserter(out), ": f64");
                    return;
                case ValueRepresentation::Pointer:
                    fmt::format_to(std::back_inserter(out), ": ptr");
                    return;
                case ValueRepresentation::None:
                case ValueRepresentation::Count:
                    break;
            }
            assert(false);
        }

        std::string buffer_string(const fmt::memory_buffer &buffer)
        {
            return {buffer.data(), buffer.size()};
        }
    }  // namespace

    std::string format_ir(const ControlFlowGraph &graph)
    {
        IRPrintState state(graph);
        fmt::memory_buffer out;
        fmt::format_to(std::back_inserter(out), "graph");
        if(graph.bytecode_state_order().has_value())
        {
            const BytecodeStateOrder &order = *graph.bytecode_state_order();
            fmt::format_to(
                std::back_inserter(out), " state(acc, thread, fp[{}..{}])",
                order.highest_frame_offset(), order.lowest_frame_offset());
        }
        fmt::format_to(std::back_inserter(out), " {{\n");

        for(size_t block_index = 0; block_index < graph.blocks().size();
            ++block_index)
        {
            const Block &block = *graph.blocks()[block_index];
            if(block_index != 0)
            {
                fmt::format_to(std::back_inserter(out), "\n");
            }
            fmt::format_to(std::back_inserter(out), "bb{}",
                           state.block_id(&block));
            if(!block.parameters().empty())
            {
                fmt::format_to(std::back_inserter(out), "(");
                for(size_t index = 0; index < block.parameters().size();
                    ++index)
                {
                    if(index != 0)
                    {
                        fmt::format_to(std::back_inserter(out), ", ");
                    }
                    print_parameter(out, state, block.parameter_at(index));
                }
                fmt::format_to(std::back_inserter(out), ")");
            }
            if(block.loop_depth() != 0)
            {
                fmt::format_to(std::back_inserter(out), " {{loop_depth = {}}}",
                               block.loop_depth());
            }
            fmt::format_to(std::back_inserter(out), ":\n");

            for(Instruction instruction: block.instructions())
            {
                fmt::format_to(std::back_inserter(out), "  ");
                print_instruction(out, state, instruction);
                fmt::format_to(std::back_inserter(out), "\n");
            }
        }
        fmt::format_to(std::back_inserter(out), "}}\n");
        return buffer_string(out);
    }

    std::string format_instruction(const ControlFlowGraph &graph,
                                   const Instruction &instruction)
    {
        IRPrintState state(graph);
        fmt::memory_buffer out;
        print_instruction(out, state, instruction);
        return buffer_string(out);
    }

}  // namespace cl::jit
