#include "codegen.h"
#include "ast.h"
#include "tokenizer.h"

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
            : av(_av), code_obj(av.compilation_unit)
        {}

        CodeObject codegen()
        {
            codegen_node(av.root_node, Mode::RValue);
            code_obj.emplace_back(0, Bytecode::Return);
            return code_obj;
        }
    private:
        enum class Mode
        {
            LValue,
            RValue
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

        void codegen_node(int32_t node_idx, Mode mode)
        {
            cl::AstKind kind = av.kinds[node_idx];
            cl::AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            switch(kind.node_kind)
            {
            case cl::AstNodeKind::EXPRESSION_BINARY:
            {

                OpTableEntry entry = get_operator_entry(kind.operator_kind);

                if(entry.binary_acc_smi != Bytecode::Invalid && av.kinds[children.rhs] == NumericalConstant && value_is_smi8(av.constants[children.rhs]))
                {
                    codegen_node(children.lhs, mode);
                    code_obj.emplace_back(source_offset, entry.binary_acc_smi, value_get_smi(av.constants[children.rhs]));
                } else if(entry.binary_smi_acc != Bytecode::Invalid && av.kinds[children.lhs] == NumericalConstant && value_is_smi8(av.constants[children.lhs]))
                {
                    codegen_node(children.rhs, mode);
                    code_obj.emplace_back(source_offset, entry.binary_smi_acc, value_get_smi(av.constants[children.lhs]));
                } else {
                    codegen_node(children.lhs, mode);
                    TemporaryReg temp_reg(this);
                    code_obj.emplace_back(source_offset, Bytecode::Star, temp_reg);

                    codegen_node(children.rhs, mode);
                    code_obj.emplace_back(source_offset, entry.standard, temp_reg);
                }
                break;
            }
            case cl::AstNodeKind::EXPRESSION_UNARY:
            {
                OpTableEntry entry = get_operator_entry(kind.operator_kind);
                codegen_node(children.lhs, mode);
                code_obj.emplace_back(source_offset, entry.standard);
                break;
            }
            case cl::AstNodeKind::EXPRESSION_LITERAL:
            {

                switch(kind.operator_kind)
                {
                case AstOperatorKind::NONE:
                    code_obj.emplace_back(source_offset, Bytecode::LdaNone);
                    break;
                case AstOperatorKind::TRUE:
                    code_obj.emplace_back(source_offset, Bytecode::LdaTrue);
                    break;
                case AstOperatorKind::FALSE:
                    code_obj.emplace_back(source_offset, Bytecode::LdaFalse);
                    break;


                case AstOperatorKind::NUMBER:
                {
                    Value val = av.constants[node_idx];
                    if(value_is_smi8(val))
                    {
                        code_obj.emplace_back(source_offset, Bytecode::LdaSmi, value_get_smi(val));
                    } else {
                        uint32_t constant_idx = code_obj.allocate_constant(val);
                        code_obj.emplace_back(source_offset, Bytecode::LdaConstant, constant_idx);
                        break;
                    }
                }
                    break;

                case AstOperatorKind::STRING:
                default:
                {
                    uint32_t constant_idx = code_obj.allocate_constant(av.constants[node_idx]);
                    code_obj.emplace_back(source_offset, Bytecode::LdaConstant, constant_idx);
                    break;
                }
                }
                break;
            }
            default:
                assert(0);
                break;
            }


        }


    };


    CodeObject generate_code(const AstVector &av)
    {

        return Codegen(av).codegen();

    }

}
