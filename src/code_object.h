#ifndef CL_CODE_OBJECT_H
#define CL_CODE_OBJECT_H

#include "bytecode.h"
#include <vector>
#include "cl_value.h"

namespace cl
{
    struct CompilationUnit;

    struct CodeObject
    {
        CodeObject(const CompilationUnit *_compilation_unit)
            : compilation_unit(_compilation_unit)
        {}

         const CompilationUnit *compilation_unit;

        uint32_t n_arguments;
        uint32_t n_temporaries;
        uint32_t n_locals;


        std::vector<uint8_t> code;

        std::vector<uint32_t> source_offsets;
        std::vector<CLValue> constant_table;

        uint32_t get_n_registers() const { return n_arguments + n_temporaries + n_locals; }

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

        uint32_t emplace_back(uint32_t source_offset, Bytecode c)
        {
            assert(c != Bytecode::Invalid);
            return emplace_back(source_offset, uint8_t(c));
        }

        uint32_t emplace_back(uint32_t source_offset, Bytecode c, uint8_t k)
        {
            assert(c != Bytecode::Invalid);
            uint32_t result = emplace_back(source_offset, uint8_t(c));
            emplace_back(source_offset, k);
            return result;
        }


        uint32_t allocate_constant(CLValue val)
        {
            uint32_t idx = constant_table.size();
            constant_table.push_back(val);
            assert(idx < 256);
            return idx;
        }

    };


}

#endif //CL_CODE_OBJECT_H
