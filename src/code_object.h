#ifndef CL_CODE_OBJECT_H
#define CL_CODE_OBJECT_H

#include "bytecode.h"
#include <vector>
#include "value.h"
#include "refcount.h"
#include "scope.h"

namespace cl
{
    struct CompilationUnit;
    struct CodeObject;

    static constexpr int32_t FrameHeaderSizeAboveFp = 2;
    static constexpr int32_t FrameHeaderSizeBelowFp = 2;
    static constexpr int32_t FrameHeaderSize = FrameHeaderSizeAboveFp + FrameHeaderSizeBelowFp;

    class JumpTarget
    {
    public:
        JumpTarget(CodeObject *_code_obj)
            : code_obj(_code_obj), target(-1)
        {}


        void resolve();

        void add_relocation(uint32_t pos)
        {
            if(target == -1)
            {
                unresolved_relocations.push_back(pos);
            } else {
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
        static constexpr Klass klass = Klass(L"CodeObject", nullptr);


        CodeObject(const CompilationUnit *_compilation_unit, Value _module_scope, Value _local_scope)
            : Object(&klass, 1, sizeof(CodeObject)/8),
              module_scope(incref(_module_scope)),
              local_scope(incref(_local_scope)),
              compilation_unit(_compilation_unit)
        {}

        Value module_scope;
        Value local_scope;
        const CompilationUnit *compilation_unit;

        uint32_t n_parameters = 0;
        uint32_t n_locals = 0;
        uint32_t n_temporaries = 0;


        std::vector<uint8_t> code;

        std::vector<uint32_t> source_offsets;
        std::vector<Value> constant_table;

        uint32_t get_n_registers() const { return n_parameters + n_temporaries + n_locals; }

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

        uint32_t emit_opcode_constant_idx(uint32_t source_offset, Bytecode c, uint8_t constant_idx)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, constant_idx);
            return result;
        }

        int8_t encode_reg(uint32_t reg)
        {
            return n_parameters - 1 + FrameHeaderSizeAboveFp - reg;
        }

        uint32_t emit_opcode_reg(uint32_t source_offset, Bytecode c, uint32_t reg)
        {
            assert(c != Bytecode::Invalid);
            int8_t encoded_reg = encode_reg(reg);
            int8_t r_offset = -encoded_reg - FrameHeaderSizeBelowFp - 1;
            if(r_offset >= 0 && r_offset < n_fastpath_ldar_star)
            {
                if(c == Bytecode::Ldar)
                {
                    return emplace_back(source_offset, uint8_t(Bytecode::Ldar0)+r_offset);
                } else if(c == Bytecode::Star)
                {
                    return emplace_back(source_offset, uint8_t(Bytecode::Star0)+r_offset);
                }
            }
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encoded_reg);
            return result;
        }

        uint32_t emit_opcode_reg_range(uint32_t source_offset, Bytecode c, uint32_t reg, uint8_t n_regs)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, encode_reg(reg));
            emplace_back(source_offset, n_regs);
            return result;
        }

        uint32_t emit_opcode_uint32(uint32_t source_offset, Bytecode c, uint32_t k)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, (k >>  0)&0xff);
            emplace_back(source_offset, (k >>  8)&0xff);
            emplace_back(source_offset, (k >> 16)&0xff);
            emplace_back(source_offset, (k >> 24)&0xff);

            return result;
        }

        uint32_t emit_jump(uint32_t source_offset, Bytecode c, JumpTarget &target)
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
            constant_table.push_back(val);
            assert(idx < 256);
            return idx;
        }


        void set_int16(uint32_t pos, int16_t v)
        {
            code[pos+0] = (v>>0)&0xff;
            code[pos+1] = (v>>8)&0xff;

        }

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


}

#endif //CL_CODE_OBJECT_H
