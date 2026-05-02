#ifndef CL_CODE_OBJECT_BUILDER_H
#define CL_CODE_OBJECT_BUILDER_H

#include "code_object.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cl
{
    class CodeObjectBuilder;

    class JumpTarget
    {
    public:
        JumpTarget(CodeObjectBuilder *_builder) : builder(_builder), target(-1)
        {
        }

        void resolve();

        void add_relocation(uint32_t pos)
        {
            if(target == -1)
            {
                unresolved_relocations.push_back(pos);
            }
            else
            {
                resolve_relocation(pos);
            }
        }

    private:
        void resolve_relocation(uint32_t pos);

        CodeObjectBuilder *builder;
        int32_t target;
        std::vector<uint32_t> unresolved_relocations;
    };

    class CodeObjectBuilder
    {
    public:
        CodeObjectBuilder(const CompilationUnit *compilation_unit,
                          Scope *module_scope, Scope *local_scope, Value name);
        CodeObjectBuilder(VirtualMachine *vm,
                          const CompilationUnit *compilation_unit,
                          Scope *module_scope, Scope *local_scope, Value name);

        CodeObjectBuilder(const CodeObjectBuilder &) = delete;
        CodeObjectBuilder &operator=(const CodeObjectBuilder &) = delete;

        CodeObjectBuilder(CodeObjectBuilder &&other) noexcept
            : code_obj(other.code_obj), finalized(other.finalized),
              outgoing_arg_relocations(
                  std::move(other.outgoing_arg_relocations))
        {
            other.code_obj = nullptr;
            other.finalized = true;
        }

        CodeObjectBuilder &operator=(CodeObjectBuilder &&other) = delete;

        Scope *get_local_scope_ptr() const
        {
            assert(code_obj != nullptr);
            return code_obj->get_local_scope_ptr();
        }

        Scope *module_scope() const
        {
            assert(code_obj != nullptr);
            return code_obj->module_scope.extract();
        }

        Scope *local_scope() const
        {
            assert(code_obj != nullptr);
            return code_obj->local_scope.extract();
        }

        Value name() const
        {
            assert(code_obj != nullptr);
            return code_obj->name.as_value();
        }

        void set_name(Value name)
        {
            assert_not_finalized();
            code_obj->name = name;
        }

        uint32_t &n_parameters()
        {
            assert_not_finalized();
            return code_obj->n_parameters;
        }

        uint32_t n_parameters() const
        {
            assert(code_obj != nullptr);
            return code_obj->n_parameters;
        }

        uint32_t &n_positional_parameters()
        {
            assert_not_finalized();
            return code_obj->n_positional_parameters;
        }

        uint32_t n_positional_parameters() const
        {
            assert(code_obj != nullptr);
            return code_obj->n_positional_parameters;
        }

        FunctionParameterFlags &parameter_flags()
        {
            assert_not_finalized();
            return code_obj->parameter_flags;
        }

        uint32_t get_padded_n_parameters() const
        {
            assert(code_obj != nullptr);
            return code_obj->get_padded_n_parameters();
        }

        uint32_t get_outgoing_arg_reg(uint32_t outgoing_slot_offset) const
        {
            assert(code_obj != nullptr);
            return code_obj->get_outgoing_arg_reg(outgoing_slot_offset);
        }

        int8_t encode_reg(uint32_t reg) const
        {
            assert(code_obj != nullptr);
            return code_obj->encode_reg(reg);
        }

        size_t size() const
        {
            assert(code_obj != nullptr);
            return code_obj->size();
        }

        uint32_t emplace_back(uint32_t source_offset, uint8_t c)
        {
            assert_not_finalized();
            uint32_t offset = code_obj->code.size();
            code_obj->code.push_back(c);
            code_obj->source_offsets.push_back(source_offset);
            return offset;
        }

        uint32_t emit_opcode(uint32_t source_offset, Bytecode c)
        {
            assert(c != Bytecode::Invalid);
            return emplace_back(source_offset, uint8_t(c));
        }

        uint32_t emit_opcode_smi(uint32_t source_offset, Bytecode c, int8_t smi)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, smi);
            return result;
        }

        uint32_t emit_opcode_constant_idx(uint32_t source_offset, Bytecode c,
                                          uint8_t constant_idx)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, constant_idx);
            return result;
        }

        uint32_t emit_opcode_constant_idx_reg(uint32_t source_offset,
                                              Bytecode c, uint8_t constant_idx,
                                              uint32_t reg)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, encode_reg(reg));
            return result;
        }

        uint32_t emit_opcode_constant_idx_reg(uint32_t source_offset,
                                              Bytecode c, uint8_t constant_idx,
                                              OutgoingArgReg reg)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, constant_idx);
            uint32_t operand_offset = code_obj->code.size();
            emplace_back(source_offset, reg.slot_offset);
            add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
            return result;
        }

        uint32_t emit_opcode_constant_idx_reg_argc(uint32_t source_offset,
                                                   Bytecode c,
                                                   uint8_t constant_idx,
                                                   OutgoingArgReg reg,
                                                   uint8_t argc)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, constant_idx);
            uint32_t operand_offset = code_obj->code.size();
            emplace_back(source_offset, reg.slot_offset);
            add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
            emplace_back(source_offset, argc);
            return result;
        }

        uint32_t emit_opcode_constant_idx_constant_idx(
            uint32_t source_offset, Bytecode c, uint8_t first_constant_idx,
            uint8_t second_constant_idx)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, first_constant_idx);
            emplace_back(source_offset, second_constant_idx);
            return result;
        }

        int8_t encode_reg(uint32_t reg)
        {
            return get_padded_n_parameters() - 1 + FrameHeaderSizeAboveFp - reg;
        }

        uint32_t emit_opcode_reg(uint32_t source_offset, Bytecode c,
                                 uint32_t reg)
        {
            assert(c != Bytecode::Invalid);
            int8_t encoded_reg = encode_reg(reg);
            int8_t r_offset = -encoded_reg - FrameHeaderSizeBelowFp - 1;
            if(r_offset >= 0 && r_offset < n_fastpath_ldar_star)
            {
                if(c == Bytecode::Ldar)
                {
                    return emplace_back(source_offset,
                                        uint8_t(Bytecode::Ldar0) + r_offset);
                }
                else if(c == Bytecode::Star)
                {
                    return emplace_back(source_offset,
                                        uint8_t(Bytecode::Star0) + r_offset);
                }
            }
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encoded_reg);
            return result;
        }

        uint32_t emit_opcode_reg(uint32_t source_offset, Bytecode c,
                                 OutgoingArgReg reg)
        {
            assert(c != Bytecode::Invalid);
            emplace_back(source_offset, uint8_t(c));
            uint32_t operand_offset = code_obj->code.size();
            emplace_back(source_offset, reg.slot_offset);
            add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
            return operand_offset - 1;
        }

        uint32_t emit_opcode_reg_range(uint32_t source_offset, Bytecode c,
                                       uint32_t reg, uint8_t n_regs)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            emplace_back(source_offset, n_regs);
            return result;
        }

        uint32_t emit_opcode_reg_range(uint32_t source_offset, Bytecode c,
                                       OutgoingArgReg reg, uint8_t n_regs)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            uint32_t operand_offset = code_obj->code.size();
            emplace_back(source_offset, reg.slot_offset);
            add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
            emplace_back(source_offset, n_regs);
            return result;
        }

        uint32_t emit_call_simple(uint32_t source_offset, uint32_t callable_reg,
                                  OutgoingArgReg first_arg_reg, uint8_t argc)
        {
            uint32_t result =
                emplace_back(source_offset, uint8_t(Bytecode::CallSimple));
            uint8_t cache_idx = allocate_function_call_cache();
            emplace_back(source_offset, encode_reg(callable_reg));
            uint32_t first_arg_operand_offset = code_obj->code.size();
            emplace_back(source_offset, first_arg_reg.slot_offset);
            add_outgoing_arg_relocation(first_arg_operand_offset,
                                        first_arg_reg.slot_offset);
            emplace_back(source_offset, argc);
            emplace_back(source_offset, cache_idx);
            return result;
        }

        uint32_t emit_opcode_native_target_idx(uint32_t source_offset,
                                               Bytecode c, uint8_t target_idx)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, target_idx);
            return result;
        }

        uint32_t add_native_function_target(NativeFunctionTarget target)
        {
            uint32_t idx = code_obj->native_function_targets.size();
            code_obj->native_function_targets.push_back(target);
            assert(idx < 256);
            return idx;
        }

        uint32_t emit_opcode_reg_constant_idx(uint32_t source_offset,
                                              Bytecode c, uint32_t reg,
                                              uint8_t constant_idx)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            emplace_back(source_offset, constant_idx);
            return result;
        }

        uint32_t emit_opcode_reg_constant_idx_cache_idx(uint32_t source_offset,
                                                        Bytecode c,
                                                        uint32_t reg,
                                                        uint8_t constant_idx,
                                                        uint8_t cache_idx)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, cache_idx);
            return result;
        }

        uint32_t emit_opcode_reg_constant_idx_argc(uint32_t source_offset,
                                                   Bytecode c, uint32_t reg,
                                                   uint8_t constant_idx,
                                                   uint8_t argc)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, argc);
            return result;
        }

        uint32_t emit_opcode_reg_constant_idx_cache_idx_argc(
            uint32_t source_offset, Bytecode c, uint32_t reg,
            uint8_t constant_idx, uint8_t read_cache_idx,
            uint8_t call_cache_idx, uint8_t argc)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, read_cache_idx);
            emplace_back(source_offset, call_cache_idx);
            emplace_back(source_offset, argc);
            return result;
        }

        uint32_t emit_opcode_reg_constant_idx_cache_idx_argc(
            uint32_t source_offset, Bytecode c, OutgoingArgReg reg,
            uint8_t constant_idx, uint8_t read_cache_idx,
            uint8_t call_cache_idx, uint8_t argc)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            uint32_t operand_offset = code_obj->code.size();
            emplace_back(source_offset, reg.slot_offset);
            add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, read_cache_idx);
            emplace_back(source_offset, call_cache_idx);
            emplace_back(source_offset, argc);
            return result;
        }

        uint32_t emit_opcode_reg_reg(uint32_t source_offset, Bytecode c,
                                     uint32_t first_reg, uint32_t second_reg)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(first_reg));
            emplace_back(source_offset, encode_reg(second_reg));
            return result;
        }

        uint32_t emit_opcode_reg_constant_idx_reg(uint32_t source_offset,
                                                  Bytecode c,
                                                  uint32_t first_reg,
                                                  uint8_t constant_idx,
                                                  uint32_t second_reg)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(first_reg));
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, encode_reg(second_reg));
            return result;
        }

        uint32_t emit_opcode_reg_jump(uint32_t source_offset, Bytecode c,
                                      uint32_t reg, JumpTarget &target)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            uint32_t pos = code_obj->code.size();
            emplace_back(source_offset, 0);
            emplace_back(source_offset, 0);
            target.add_relocation(pos);

            return result;
        }

        uint32_t emit_opcode_uint32(uint32_t source_offset, Bytecode c,
                                    uint32_t k)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, (k >> 0) & 0xff);
            emplace_back(source_offset, (k >> 8) & 0xff);
            emplace_back(source_offset, (k >> 16) & 0xff);
            emplace_back(source_offset, (k >> 24) & 0xff);

            return result;
        }

        uint32_t emit_jump(uint32_t source_offset, Bytecode c,
                           JumpTarget &target)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            uint32_t pos = code_obj->code.size();
            emplace_back(source_offset, 0);
            emplace_back(source_offset, 0);
            target.add_relocation(pos);

            return result;
        }

        uint32_t allocate_constant(Value val)
        {
            uint32_t idx = code_obj->constant_table.size();
            code_obj->constant_table.emplace_back(val);
            assert(idx < 256);
            return idx;
        }

        uint32_t allocate_attribute_read_cache()
        {
            uint32_t idx = code_obj->attribute_read_caches.size();
            code_obj->attribute_read_caches.emplace_back();
            assert(idx < 256);
            return idx;
        }

        uint32_t allocate_attribute_mutation_cache()
        {
            uint32_t idx = code_obj->attribute_mutation_caches.size();
            code_obj->attribute_mutation_caches.emplace_back();
            assert(idx < 256);
            return idx;
        }

        uint32_t allocate_function_call_cache()
        {
            uint32_t idx = code_obj->function_call_caches.size();
            code_obj->function_call_caches.emplace_back();
            assert(idx < 256);
            return idx;
        }

        void set_int16(uint32_t pos, int16_t v)
        {
            code_obj->code[pos + 0] = (v >> 0) & 0xff;
            code_obj->code[pos + 1] = (v >> 8) & 0xff;
        }

        void set_encoded_reg(uint32_t pos, uint32_t reg)
        {
            code_obj->code[pos] = encode_reg(reg);
        }

        CodeObject *finalize(uint32_t max_temporary_reg)
        {
            assert_not_finalized();
            uint32_t local_scope_size = FrameHeaderSize;
            if(code_obj->local_scope != nullptr)
            {
                local_scope_size = get_local_scope_ptr()->size();
                uint32_t named_local_and_header_slots =
                    local_scope_size - get_padded_n_parameters();
                assert(named_local_and_header_slots >= FrameHeaderSize);
                code_obj->n_locals =
                    named_local_and_header_slots - FrameHeaderSize;
            }
            assert(max_temporary_reg >= local_scope_size);
            code_obj->n_temporaries = max_temporary_reg - local_scope_size;
            patch_outgoing_arg_relocations();
            finalized = true;
            return code_obj;
        }

    private:
        void assert_not_finalized() const
        {
            assert(code_obj != nullptr);
            assert(!finalized);
        }

        void patch_outgoing_arg_relocations()
        {
            for(const OutgoingArgRelocation &reloc: outgoing_arg_relocations)
            {
                set_encoded_reg(
                    reloc.operand_offset,
                    get_outgoing_arg_reg(code_obj->code[reloc.operand_offset]));
            }
        }

        void add_outgoing_arg_relocation(uint32_t operand_offset,
                                         uint32_t outgoing_slot_offset)
        {
            assert(outgoing_slot_offset < 256);
            outgoing_arg_relocations.push_back({operand_offset});
            code_obj->n_outgoing_call_slots =
                std::max(code_obj->n_outgoing_call_slots,
                         round_up_to_abi_alignment(outgoing_slot_offset + 1));
        }

        CodeObject *code_obj;
        bool finalized = false;
        struct OutgoingArgRelocation
        {
            uint32_t operand_offset;
        };

        std::vector<OutgoingArgRelocation> outgoing_arg_relocations;
    };

    inline void JumpTarget::resolve_relocation(uint32_t pos)
    {
        int32_t rel_dest = target - (pos + 2);
        if(rel_dest != int16_t(rel_dest))
        {
            throw std::runtime_error("Relocation out of range");
        }
        builder->set_int16(pos, rel_dest);
    }

    inline void JumpTarget::resolve()
    {
        target = builder->size();
        for(uint32_t pos: unresolved_relocations)
        {
            resolve_relocation(pos);
        }
        unresolved_relocations.clear();
    }

}  // namespace cl

#endif  // CL_CODE_OBJECT_BUILDER_H
