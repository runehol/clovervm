#ifndef CL_CODE_OBJECT_H
#define CL_CODE_OBJECT_H

#include "attribute_cache.h"
#include "builtin_class_registry.h"
#include "bytecode.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "scope.h"
#include "value.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace cl
{
    enum class FunctionParameterFlags : uint32_t
    {
        None = 0,
        HasVarArgs = 1U << 0,
    };

    inline FunctionParameterFlags operator|(FunctionParameterFlags lhs,
                                            FunctionParameterFlags rhs)
    {
        return FunctionParameterFlags(uint32_t(lhs) | uint32_t(rhs));
    }

    inline FunctionParameterFlags &operator|=(FunctionParameterFlags &lhs,
                                              FunctionParameterFlags rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    inline bool has_function_parameter_flag(FunctionParameterFlags flags,
                                            FunctionParameterFlags flag)
    {
        return (uint32_t(flags) & uint32_t(flag)) != 0;
    }

    using NativeFunction0 = Value (*)();
    using NativeFunction1 = Value (*)(Value);
    using NativeFunction2 = Value (*)(Value, Value);

    union NativeFunctionTarget
    {
        NativeFunction0 fixed0;
        NativeFunction1 fixed1;
        NativeFunction2 fixed2;
    };

    struct CompilationUnit;
    struct CodeObject;
    class Function;
    class ClassObject;
    class VirtualMachine;

    enum class FunctionCallAdaptation : uint8_t
    {
        FixedArity,
        Defaults,
        Varargs,
    };

    struct FunctionCallInlineCache
    {
        Function *function = nullptr;
        CodeObject *code_object = nullptr;
        uint8_t n_args = 0;
        FunctionCallAdaptation adaptation = FunctionCallAdaptation::FixedArity;
    };

    struct OutgoingArgReg
    {
        explicit OutgoingArgReg(uint32_t _slot_offset)
            : slot_offset(_slot_offset)
        {
        }

        uint32_t slot_offset;
    };

    static constexpr int32_t FrameHeaderSizeAboveFp = 2;
    static constexpr int32_t FrameHeaderSizeBelowFp = 2;
    static constexpr int32_t FrameHeaderSize =
        FrameHeaderSizeAboveFp + FrameHeaderSizeBelowFp;

    constexpr uint32_t round_up_to_abi_alignment(uint32_t value)
    {
        return (value + 1u) & ~1u;
    }

    static_assert(round_up_to_abi_alignment(0) == 0);
    static_assert(round_up_to_abi_alignment(1) == 2);
    static_assert(round_up_to_abi_alignment(2) == 2);
    static_assert(round_up_to_abi_alignment(3) == 4);

    class JumpTarget
    {
    public:
        JumpTarget(CodeObject *_code_obj) : code_obj(_code_obj), target(-1) {}

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

        CodeObject *code_obj;
        int32_t target;
        std::vector<uint32_t> unresolved_relocations;
    };

    struct CodeObject : public Object
    {
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::CodeObject;

        CodeObject(ClassObject *cls, const CompilationUnit *_compilation_unit,
                   Scope *_module_scope, Scope *_local_scope, Value _name)
            : Object(cls, native_layout_id, compact_layout()),
              module_scope(_module_scope), local_scope(_local_scope),
              name(_name), compilation_unit(_compilation_unit)
        {
        }

        MemberHeapPtr<Scope> module_scope;
        MemberHeapPtr<Scope> local_scope;
        MemberValue name;
        const CompilationUnit *compilation_unit;

        uint32_t n_parameters = 0;
        uint32_t n_positional_parameters = 0;
        FunctionParameterFlags parameter_flags = FunctionParameterFlags::None;
        uint32_t n_locals = 0;
        uint32_t n_temporaries = 0;
        uint32_t n_outgoing_call_slots = 0;

        Scope *get_local_scope_ptr() const { return local_scope.extract(); }

        bool has_varargs() const
        {
            return has_function_parameter_flag(
                parameter_flags, FunctionParameterFlags::HasVarArgs);
        }

        std::vector<uint8_t> code;

        std::vector<uint32_t> source_offsets;
        std::vector<OwnedValue> constant_table;
        std::vector<AttributeReadInlineCache> attribute_read_caches;
        std::vector<AttributeWriteInlineCache> attribute_write_caches;
        std::vector<FunctionCallInlineCache> function_call_caches;
        std::vector<NativeFunctionTarget> native_function_targets;

        struct OutgoingArgRelocation
        {
            uint32_t operand_offset;
        };

        std::vector<OutgoingArgRelocation> outgoing_arg_relocations;

        uint32_t get_n_registers() const
        {
            return n_parameters + n_temporaries + n_locals +
                   n_outgoing_call_slots;
        }

        uint32_t get_padded_n_parameters() const
        {
            return round_up_to_abi_alignment(n_parameters);
        }

        uint32_t get_padded_n_ordinary_below_frame_slots() const
        {
            return round_up_to_abi_alignment(n_locals + n_temporaries);
        }

        uint32_t get_outgoing_arg_reg(uint32_t outgoing_slot_offset) const
        {
            return get_padded_n_parameters() + FrameHeaderSize +
                   get_padded_n_ordinary_below_frame_slots() +
                   outgoing_slot_offset;
        }

        int32_t get_highest_occupied_frame_offset() const
        {
            if(n_parameters == 0)
            {
                return 0;
            }
            return int32_t(FrameHeaderSizeAboveFp + get_padded_n_parameters() -
                           1);
        }

        size_t size() const
        {
            assert(code.size() == source_offsets.size());
            return code.size();
        }

        uint32_t emplace_back(uint32_t source_offset, uint8_t c)
        {
            uint32_t offset = code.size();
            code.push_back(c);
            source_offsets.push_back(source_offset);
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
            uint32_t operand_offset = code.size();
            emplace_back(source_offset, reg.slot_offset);
            add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
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
            uint32_t operand_offset = code.size();
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
            uint32_t operand_offset = code.size();
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
            uint32_t first_arg_operand_offset = code.size();
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
            uint32_t idx = native_function_targets.size();
            native_function_targets.push_back(target);
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
            uint8_t constant_idx, uint8_t cache_idx, uint8_t argc)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, cache_idx);
            emplace_back(source_offset, argc);
            return result;
        }

        uint32_t emit_opcode_reg_constant_idx_cache_idx_argc(
            uint32_t source_offset, Bytecode c, OutgoingArgReg reg,
            uint8_t constant_idx, uint8_t cache_idx, uint8_t argc)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            uint32_t operand_offset = code.size();
            emplace_back(source_offset, reg.slot_offset);
            add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
            emplace_back(source_offset, constant_idx);
            emplace_back(source_offset, cache_idx);
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
            uint32_t pos = code.size();
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
            uint32_t pos = code.size();
            emplace_back(source_offset, 0);
            emplace_back(source_offset, 0);
            target.add_relocation(pos);

            return result;
        }

        uint32_t allocate_constant(Value val)
        {
            uint32_t idx = constant_table.size();
            constant_table.emplace_back(val);
            assert(idx < 256);
            return idx;
        }

        uint32_t allocate_attribute_read_cache()
        {
            uint32_t idx = attribute_read_caches.size();
            attribute_read_caches.emplace_back();
            assert(idx < 256);
            return idx;
        }

        uint32_t allocate_attribute_write_cache()
        {
            uint32_t idx = attribute_write_caches.size();
            attribute_write_caches.emplace_back();
            assert(idx < 256);
            return idx;
        }

        uint32_t allocate_function_call_cache()
        {
            uint32_t idx = function_call_caches.size();
            function_call_caches.emplace_back();
            assert(idx < 256);
            return idx;
        }

        void set_int16(uint32_t pos, int16_t v)
        {
            code[pos + 0] = (v >> 0) & 0xff;
            code[pos + 1] = (v >> 8) & 0xff;
        }

        void set_encoded_reg(uint32_t pos, uint32_t reg)
        {
            code[pos] = encode_reg(reg);
        }

        void finalize(uint32_t max_temporary_reg)
        {
            uint32_t local_scope_size = FrameHeaderSize;
            if(local_scope != nullptr)
            {
                local_scope_size = get_local_scope_ptr()->size();
                uint32_t named_local_and_header_slots =
                    local_scope_size - get_padded_n_parameters();
                assert(named_local_and_header_slots >= FrameHeaderSize);
                n_locals = named_local_and_header_slots - FrameHeaderSize;
            }
            assert(max_temporary_reg >= local_scope_size);
            n_temporaries = max_temporary_reg - local_scope_size;
            patch_outgoing_arg_relocations();
        }

    private:
        void patch_outgoing_arg_relocations()
        {
            for(const OutgoingArgRelocation &reloc: outgoing_arg_relocations)
            {
                set_encoded_reg(
                    reloc.operand_offset,
                    get_outgoing_arg_reg(code[reloc.operand_offset]));
            }
        }

        void add_outgoing_arg_relocation(uint32_t operand_offset,
                                         uint32_t outgoing_slot_offset)
        {
            assert(outgoing_slot_offset < 256);
            outgoing_arg_relocations.push_back({operand_offset});
            n_outgoing_call_slots =
                std::max(n_outgoing_call_slots,
                         round_up_to_abi_alignment(outgoing_slot_offset + 1));
        }

    public:
        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(CodeObject, Object, 3);
    };

    inline void JumpTarget::resolve_relocation(uint32_t pos)
    {
        int32_t rel_dest = target - (pos + 2);
        if(rel_dest != int16_t(rel_dest))
        {
            throw std::runtime_error("Relocation out of range");
        }
        code_obj->set_int16(pos, rel_dest);
    }

    inline void JumpTarget::resolve()
    {
        target = code_obj->size();
        for(uint32_t pos: unresolved_relocations)
        {
            resolve_relocation(pos);
        }
        unresolved_relocations.clear();
    }

    BuiltinClassDefinition make_code_object_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_CODE_OBJECT_H
