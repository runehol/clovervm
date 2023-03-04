#include "codegen.h"
#include "ast.h"
#include "tokenizer.h"
#include "scope.h"
#include "thread_state.h"
#include <fmt/core.h>

namespace cl
{

    struct OpTableEntry
    {
        constexpr OpTableEntry(Bytecode _standard = Bytecode::Invalid, Bytecode _binary_acc_smi = Bytecode::Invalid, Bytecode _binary_smi_acc = Bytecode::Invalid)
            : standard(_standard),
              binary_acc_smi(_binary_acc_smi),
              binary_smi_acc(_binary_smi_acc)
        {}

        Bytecode standard;
        Bytecode binary_acc_smi;
        Bytecode binary_smi_acc;
    };

    struct OpTable
    {
        OpTableEntry table[AstOperatorKindSize];
    };


    constexpr OpTable make_table()
    {
        OpTable t;

        t.table[size_t(AstOperatorKind::ADD)] = OpTableEntry(Bytecode::Add, Bytecode::AddSmi, Bytecode::AddSmi);
        t.table[size_t(AstOperatorKind::SUBTRACT)] = OpTableEntry(Bytecode::Sub, Bytecode::SubSmi, Bytecode::Invalid);
        t.table[size_t(AstOperatorKind::MULTIPLY)] = OpTableEntry(Bytecode::Mul, Bytecode::MulSmi, Bytecode::MulSmi);
        t.table[size_t(AstOperatorKind::DIVIDE)] = OpTableEntry(Bytecode::Div, Bytecode::DivSmi);
        t.table[size_t(AstOperatorKind::INT_DIVIDE)] = OpTableEntry(Bytecode::IntDiv, Bytecode::IntDivSmi);
        t.table[size_t(AstOperatorKind::POWER)] = OpTableEntry(Bytecode::Pow, Bytecode::PowSmi);
        t.table[size_t(AstOperatorKind::LEFTSHIFT)] = OpTableEntry(Bytecode::LeftShift, Bytecode::LeftShiftSmi);
        t.table[size_t(AstOperatorKind::RIGHTSHIFT)] = OpTableEntry(Bytecode::RightShift, Bytecode::RightShiftSmi);
        t.table[size_t(AstOperatorKind::MODULO)] = OpTableEntry(Bytecode::Mod, Bytecode::ModSmi);
        t.table[size_t(AstOperatorKind::BITWISE_OR)] = OpTableEntry(Bytecode::BitwiseOr, Bytecode::BitwiseOrSmi, Bytecode::BitwiseOrSmi);
        t.table[size_t(AstOperatorKind::BITWISE_AND)] = OpTableEntry(Bytecode::BitwiseAnd, Bytecode::BitwiseAndSmi, Bytecode::BitwiseAndSmi);
        t.table[size_t(AstOperatorKind::BITWISE_XOR)] = OpTableEntry(Bytecode::BitwiseXor, Bytecode::BitwiseXorSmi, Bytecode::BitwiseXorSmi);


        t.table[size_t(AstOperatorKind::NOT)] = OpTableEntry(Bytecode::Not);
        t.table[size_t(AstOperatorKind::NEGATE)] = OpTableEntry(Bytecode::Negate);
        t.table[size_t(AstOperatorKind::PLUS)] = OpTableEntry(Bytecode::Plus);
        t.table[size_t(AstOperatorKind::BITWISE_NOT)] = OpTableEntry(Bytecode::BitwiseNot);


        return t;

    }



    class Codegen
    {
    public:
        Codegen(const AstVector &_av)
            : av(_av), code_obj(av.compilation_unit, new(ThreadState::get_active()->allocate_refcounted(sizeof(Scope)))Scope(Value::None()))
        {}

        CodeObject codegen()
        {
            active_scope = code_obj.module_scope;
            codegen_node(av.root_node, Mode::Module);
            code_obj.emit_opcode(0, Bytecode::Return);
            return code_obj;
        }
    private:
        Scope *active_scope = nullptr;

        enum class Mode
        {
            Module,
            Class,
            Function
        };

        constexpr static OpTable operator_table = make_table();

        constexpr static OpTableEntry get_operator_entry(AstOperatorKind ok)
        {
            return operator_table.table[size_t(ok)];
        }

        const AstVector &av;
        CodeObject code_obj;

        uint32_t _temporary_reg = 0;

        class TemporaryReg
        {
        public:
            friend class Codegen;
            TemporaryReg(Codegen *_cg)
                : cg(_cg)
            {
                reg = cg->_temporary_reg++;
            }

            ~TemporaryReg()
            {
                --cg->_temporary_reg;
                assert(reg == cg->_temporary_reg);
            }

            operator uint32_t() const { return reg; }
        private:
            uint32_t reg;
            Codegen *cg;

        };

        static constexpr AstKind NumericalConstant = AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NUMBER);

        // used for both regular binary expressions and augmented assignment, so pull out
        void codegen_binary_expression(int32_t node_idx, Mode mode)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            OpTableEntry entry = get_operator_entry(kind.operator_kind);

            if(entry.binary_acc_smi != Bytecode::Invalid && av.kinds[children[1]] == NumericalConstant && av.constants[children[1]].is_smi8())
            {
                codegen_node(children[0], mode);
                code_obj.emit_opcode_uint8(source_offset, entry.binary_acc_smi, av.constants[children[1]].get_smi());
            } else if(entry.binary_smi_acc != Bytecode::Invalid && av.kinds[children[0]] == NumericalConstant && av.constants[children[0]].is_smi8())
            {
                codegen_node(children[1], mode);
                code_obj.emit_opcode_uint8(source_offset, entry.binary_smi_acc, av.constants[children[0]].get_smi());
            } else {
                codegen_node(children[0], mode);
                TemporaryReg temp_reg(this);
                code_obj.emit_opcode_uint8(source_offset, Bytecode::Star, temp_reg);

                codegen_node(children[1], mode);
                code_obj.emit_opcode_uint8(source_offset, entry.standard, temp_reg);
            }

        }

        void codegen_node(int32_t node_idx, Mode mode)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            switch(kind.node_kind)
            {

            case AstNodeKind::EXPRESSION_VARIABLE_REFERENCE:
            {
                uint32_t slot_idx = code_obj.module_scope->register_slot_index_for_read(av.constants[node_idx]);
                code_obj.emit_opcode_uint32(source_offset, Bytecode::LdaGlobal, slot_idx);
                break;
            }

            case AstNodeKind::EXPRESSION_ASSIGN:
            {

                int32_t lhs_idx = children[0];
                if(av.kinds[lhs_idx].node_kind != AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                {
                    throw std::runtime_error("We don't support assignment to anything but simple variables yet");
                }
                uint32_t slot_idx = code_obj.module_scope->register_slot_index_for_write(av.constants[lhs_idx]);


                // augmented assignment
                if(kind.operator_kind != AstOperatorKind::NOP)
                {
                    codegen_binary_expression(node_idx, mode);
                } else {
                    // just compute the RHS
                    codegen_node(children[1], mode);
                }
                code_obj.emit_opcode_uint32(source_offset, Bytecode::StaGlobal, slot_idx);
                break;

            }


            case AstNodeKind::EXPRESSION_BINARY:
                codegen_binary_expression(node_idx, mode);
                break;


            case AstNodeKind::EXPRESSION_UNARY:
            {
                OpTableEntry entry = get_operator_entry(kind.operator_kind);
                codegen_node(children[0], mode);
                code_obj.emit_opcode(source_offset, entry.standard);
                break;
            }
            case AstNodeKind::EXPRESSION_LITERAL:
            {

                switch(kind.operator_kind)
                {
                case AstOperatorKind::NONE:
                    code_obj.emit_opcode(source_offset, Bytecode::LdaNone);
                    break;
                case AstOperatorKind::TRUE:
                    code_obj.emit_opcode(source_offset, Bytecode::LdaTrue);
                    break;
                case AstOperatorKind::FALSE:
                    code_obj.emit_opcode(source_offset, Bytecode::LdaFalse);
                    break;


                case AstOperatorKind::NUMBER:
                {
                    Value val = av.constants[node_idx];
                    if(val.is_smi8())
                    {
                        code_obj.emit_opcode_uint8(source_offset, Bytecode::LdaSmi, val.get_smi());
                    } else {
                        uint32_t constant_idx = code_obj.allocate_constant(val);
                        code_obj.emit_opcode_uint8(source_offset, Bytecode::LdaConstant, constant_idx);
                        break;
                    }
                    break;
                }

                case AstOperatorKind::STRING:
                {
                    uint32_t constant_idx = code_obj.allocate_constant(av.constants[node_idx]);
                    code_obj.emit_opcode_uint8(source_offset, Bytecode::LdaConstant, constant_idx);
                    break;
                }
                default:
                    break;
                }
                break;
            }

            case AstNodeKind::STATEMENT_SEQUENCE:
                for(int32_t ch_idx: children)
                {
                    codegen_node(ch_idx, mode);
                }
                break;

            default:
                throw std::runtime_error(std::string("Don't know how to codegen for kind ") + std::to_string(int(kind.node_kind)));
                break;
            }


        }


    };


    CodeObject generate_code(const AstVector &av)
    {

        return Codegen(av).codegen();

    }

}
