#include "codegen.h"
#include "ast.h"
#include "tokenizer.h"
#include "scope.h"
#include "thread_state.h"
#include <fmt/core.h>
#include <optional>

namespace cl
{

    struct OpTableEntry
    {
        constexpr OpTableEntry(Bytecode _standard = Bytecode::Invalid, Bytecode _binary_acc_smi = Bytecode::Invalid)
            : standard(_standard),
              binary_acc_smi(_binary_acc_smi)
        {}

        Bytecode standard;
        Bytecode binary_acc_smi;
    };

    struct OpTable
    {
        OpTableEntry table[AstOperatorKindSize];
    };


    constexpr OpTable make_table()
    {
        OpTable t;

        t.table[size_t(AstOperatorKind::ADD)] = OpTableEntry(Bytecode::Add, Bytecode::AddSmi);
        t.table[size_t(AstOperatorKind::SUBTRACT)] = OpTableEntry(Bytecode::Sub, Bytecode::SubSmi);
        t.table[size_t(AstOperatorKind::MULTIPLY)] = OpTableEntry(Bytecode::Mul, Bytecode::MulSmi);
        t.table[size_t(AstOperatorKind::DIVIDE)] = OpTableEntry(Bytecode::Div, Bytecode::DivSmi);
        t.table[size_t(AstOperatorKind::INT_DIVIDE)] = OpTableEntry(Bytecode::IntDiv, Bytecode::IntDivSmi);
        t.table[size_t(AstOperatorKind::POWER)] = OpTableEntry(Bytecode::Pow, Bytecode::PowSmi);
        t.table[size_t(AstOperatorKind::LEFTSHIFT)] = OpTableEntry(Bytecode::LeftShift, Bytecode::LeftShiftSmi);
        t.table[size_t(AstOperatorKind::RIGHTSHIFT)] = OpTableEntry(Bytecode::RightShift, Bytecode::RightShiftSmi);
        t.table[size_t(AstOperatorKind::MODULO)] = OpTableEntry(Bytecode::Mod, Bytecode::ModSmi);
        t.table[size_t(AstOperatorKind::BITWISE_OR)] = OpTableEntry(Bytecode::BitwiseOr, Bytecode::BitwiseOrSmi);
        t.table[size_t(AstOperatorKind::BITWISE_AND)] = OpTableEntry(Bytecode::BitwiseAnd, Bytecode::BitwiseAndSmi);
        t.table[size_t(AstOperatorKind::BITWISE_XOR)] = OpTableEntry(Bytecode::BitwiseXor, Bytecode::BitwiseXorSmi);


        t.table[size_t(AstOperatorKind::EQUAL)] = OpTableEntry(Bytecode::TestEqual);
        t.table[size_t(AstOperatorKind::NOT_EQUAL)] = OpTableEntry(Bytecode::TestNotEqual);
        t.table[size_t(AstOperatorKind::LESS)] = OpTableEntry(Bytecode::TestLess);
        t.table[size_t(AstOperatorKind::LESS_EQUAL)] = OpTableEntry(Bytecode::TestLessEqual);
        t.table[size_t(AstOperatorKind::GREATER)] = OpTableEntry(Bytecode::TestGreater);
        t.table[size_t(AstOperatorKind::GREATER_EQUAL)] = OpTableEntry(Bytecode::TestGreaterEqual);
        t.table[size_t(AstOperatorKind::IS)] = OpTableEntry(Bytecode::TestIs);
        t.table[size_t(AstOperatorKind::IS_NOT)] = OpTableEntry(Bytecode::TestIsNot);
        t.table[size_t(AstOperatorKind::IN)] = OpTableEntry(Bytecode::TestIn);
        t.table[size_t(AstOperatorKind::NOT_IN)] = OpTableEntry(Bytecode::TestNotIn);


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
            : av(_av), module_scope(Value::from_oop(new(ThreadState::get_active()->allocate_refcounted(sizeof(Scope)))Scope(Value::None())))

        {}

        CodeObject *codegen()
        {
            code_obj = make_code_obj(Mode::Module);
            codegen_node(av.root_node, Mode::Module);
            code_obj->emit_opcode(0, Bytecode::Halt);
            return incref(code_obj);
        }
    private:

        enum class Mode
        {
            Module,
            Class,
            Function
        };

        CodeObject *make_code_obj(Mode mode)
        {
            ThreadState *ts = ThreadState::get_active();
            Value local_scope = Value::None();
            switch(mode)
            {
            case Mode::Module:
                break;

            case Mode::Class:
            case Mode::Function:
                local_scope = Value::from_oop(new(ts->allocate_refcounted(sizeof(Scope)))Scope(code_obj->local_scope));
                break;
            }

            CodeObject *code_obj = new(ts->allocate_refcounted(sizeof(CodeObject)))CodeObject(av.compilation_unit, module_scope, local_scope);
            return code_obj;
        }

        struct LoopTargetSet
        {
            LoopTargetSet(JumpTarget *_break_target, JumpTarget *_continue_target)
                : break_target(_break_target), continue_target(_continue_target)
            {}

            JumpTarget *break_target;
            JumpTarget *continue_target;
        };
        std::vector<LoopTargetSet> loop_targets;

        constexpr static OpTable operator_table = make_table();

        constexpr static OpTableEntry get_operator_entry(AstOperatorKind ok)
        {
            return operator_table.table[size_t(ok)];
        }

        const AstVector &av;
        Value module_scope = Value::None();
        CodeObject *code_obj = nullptr;

        uint32_t _temporary_reg = FrameHeaderSize;

        class TemporaryReg
        {
        public:
            friend class Codegen;
            TemporaryReg(Codegen *_cg, uint32_t _n_regs=1)
                : cg(_cg), n_regs(_n_regs)
            {
                reg = cg->_temporary_reg;
                cg->_temporary_reg += n_regs;
            }

            ~TemporaryReg()
            {
                cg->_temporary_reg -= n_regs;
                assert(reg == cg->_temporary_reg);
            }

            operator uint32_t() const { return reg; }
        private:
            Codegen *cg;
            uint32_t n_regs;
            uint32_t reg;

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
                code_obj->emit_opcode_smi(source_offset, entry.binary_acc_smi, av.constants[children[1]].get_smi());
            } else {
                codegen_node(children[0], mode);
                TemporaryReg temp_reg(this);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star, temp_reg);

                codegen_node(children[1], mode);
                code_obj->emit_opcode_reg(source_offset, entry.standard, temp_reg);
            }

        }


        void codegen_comparison_fragment(int32_t node_idx, Mode mode, int32_t recv, int32_t prod)
        {
            AstKind kind = av.kinds[node_idx];
            assert(kind.node_kind == AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT);
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            OpTableEntry entry = get_operator_entry(kind.operator_kind);

            codegen_node(children[0], mode);
            if(prod >= 0)
            {
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star, prod);
            }
            code_obj->emit_opcode_reg(source_offset, entry.standard, recv);

        }

        void codegen_function_definition(int32_t node_idx, Mode mode)
        {
            // function definitions are involved enough that we prefer a separate function for it
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            uint32_t slot_idx = prepare_variable_assignment(av.constants[node_idx], mode);

            CodeObject *outer_obj = code_obj;
            CodeObject *fun_obj = make_code_obj(Mode::Function);
            uint32_t outer_temporary_reg = _temporary_reg;
            {
                code_obj = fun_obj;
                /*
                  Now we're generating code for the function
                */
                AstChildren param_children = av.children[children[0]];
                code_obj->n_parameters = param_children.size();
                for(int32_t ch: param_children)
                {
                    code_obj->local_scope.get_ptr<Scope>()->register_slot_index_for_write(av.constants[ch]);
                }
                // reserve space for the frame header
                code_obj->local_scope.get_ptr<Scope>()->reserve_empty_slots(FrameHeaderSize);


                // todo scan for local variables

                _temporary_reg = code_obj->local_scope.get_ptr<Scope>()->size();

                //now generate code for the body
                codegen_node(children[1], Mode::Function);
                // finally, emit return None just in case. as a future optimisation, we could check that all return paths already have a return statement
                code_obj->emit_opcode(source_offset, Bytecode::LdaNone);
                code_obj->emit_opcode(source_offset, Bytecode::Return);

            }
            code_obj = outer_obj;
            _temporary_reg = outer_temporary_reg;

            // stick this code object into the constant table, load it, and call the
            uint32_t constant_idx = code_obj->allocate_constant(Value::from_oop(fun_obj));
            code_obj->emit_opcode_constant_idx(source_offset, Bytecode::CreateFunction, constant_idx);


            perform_variable_assignment(source_offset, slot_idx, mode);
        }

        void codegen_function_call(int32_t node_idx, Mode mode)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            AstChildren args = av.children[children[1]];

            TemporaryReg regs(this, 1+args.size()); // a register for the function itself and all arguments

            // function itself
            codegen_node(children[0], mode);
            code_obj->emit_opcode_reg(source_offset, Bytecode::Star, regs+0);

            for(size_t i = 0; i < args.size(); ++i)
            {
                codegen_node(args[i], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star, regs+1+i);
            }
            code_obj->emit_opcode_reg_range(source_offset, Bytecode::CallSimple, regs, args.size());

        }



        uint32_t prepare_variable_assignment(Value var_name, Mode mode)
        {
            switch(mode)
            {
            case Mode::Module:
                return code_obj->module_scope.get_ptr<Scope>()->register_slot_index_for_write(var_name);

            case Mode::Class:
            case Mode::Function:
                return code_obj->local_scope.get_ptr<Scope>()->register_slot_index_for_write(var_name);
            }
        }

        void perform_variable_assignment(uint32_t source_offset, uint32_t slot_idx, Mode mode)
        {
            switch(mode)
            {
            case Mode::Module:
                code_obj->emit_opcode_uint32(source_offset, Bytecode::StaGlobal, slot_idx);
                break;
            case Mode::Class:
            case Mode::Function:
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star, slot_idx);
                break;
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
                switch(mode)
                {
                case Mode::Class:
                case Mode::Function:
                {
                    int32_t slot_idx = code_obj->local_scope.get_ptr<Scope>()->lookup_slot_index_local(av.constants[node_idx]);
                    if(slot_idx >= 0)
                    {
                        code_obj->emit_opcode_reg(source_offset, Bytecode::Ldar, slot_idx);
                        break;
                    }
                    /* otherwise, fallthrough */
                }
                case Mode::Module:
                {
                    uint32_t slot_idx = code_obj->module_scope.get_ptr<Scope>()->register_slot_index_for_read(av.constants[node_idx]);
                    code_obj->emit_opcode_uint32(source_offset, Bytecode::LdaGlobal, slot_idx);
                    break;
                }
                }
                break;

            case AstNodeKind::STATEMENT_ASSIGN:
            case AstNodeKind::EXPRESSION_ASSIGN:
            {

                int32_t lhs_idx = children[0];
                if(av.kinds[lhs_idx].node_kind != AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                {
                    throw std::runtime_error("We don't support assignment to anything but simple variables yet");
                }
                uint32_t slot_idx = prepare_variable_assignment(av.constants[lhs_idx], mode);


                // augmented assignment
                if(kind.operator_kind != AstOperatorKind::NOP)
                {
                    codegen_binary_expression(node_idx, mode);
                } else {
                    // just compute the RHS
                    codegen_node(children[1], mode);
                }
                perform_variable_assignment(source_offset, slot_idx, mode);
                break;

            }


            case AstNodeKind::EXPRESSION_BINARY:
                codegen_binary_expression(node_idx, mode);
                break;


            case AstNodeKind::EXPRESSION_UNARY:
            {
                OpTableEntry entry = get_operator_entry(kind.operator_kind);
                codegen_node(children[0], mode);
                code_obj->emit_opcode(source_offset, entry.standard);
                break;
            }
            case AstNodeKind::EXPRESSION_LITERAL:
            {

                switch(kind.operator_kind)
                {
                case AstOperatorKind::NONE:
                    code_obj->emit_opcode(source_offset, Bytecode::LdaNone);
                    break;
                case AstOperatorKind::TRUE:
                    code_obj->emit_opcode(source_offset, Bytecode::LdaTrue);
                    break;
                case AstOperatorKind::FALSE:
                    code_obj->emit_opcode(source_offset, Bytecode::LdaFalse);
                    break;


                case AstOperatorKind::NUMBER:
                {
                    Value val = av.constants[node_idx];
                    if(val.is_smi8())
                    {
                        code_obj->emit_opcode_smi(source_offset, Bytecode::LdaSmi, val.get_smi());
                    } else {
                        uint32_t constant_idx = code_obj->allocate_constant(val);
                        code_obj->emit_opcode_constant_idx(source_offset, Bytecode::LdaConstant, constant_idx);
                        break;
                    }
                    break;
                }

                case AstOperatorKind::STRING:
                {
                    uint32_t constant_idx = code_obj->allocate_constant(av.constants[node_idx]);
                    code_obj->emit_opcode_constant_idx(source_offset, Bytecode::LdaConstant, constant_idx);
                    break;
                }
                default:
                    break;
                }
                break;
            }

            case AstNodeKind::EXPRESSION_COMPARISON:
            {
                JumpTarget skip_target(code_obj);

                uint32_t n_temporaries = children.size() > 2 ? 2 : 1;
                TemporaryReg regs(this, n_temporaries);
                int32_t recv = regs;
                int32_t prod = regs + 1;

                codegen_node(children[0], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star, recv);
                for(size_t i = 1; i < children.size(); ++i)
                {
                    bool last = i == children.size() - 1;
                    if(last) prod = -1;

                    codegen_comparison_fragment(children[i], mode, recv, prod);

                    if(!last)
                    {
                        code_obj->emit_jump(source_offset, Bytecode::JumpIfFalse, skip_target);
                    }
                    std::swap(recv, prod);
                }
                skip_target.resolve();

                break;
            }


            case AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY:
            {
                JumpTarget skip_target(code_obj);
                codegen_node(children[0], mode);
                switch(kind.operator_kind)
                {
                case AstOperatorKind::SHORTCUTTING_AND:
                    code_obj->emit_jump(source_offset, Bytecode::JumpIfFalse, skip_target);
                    break;
                case AstOperatorKind::SHORTCUTTING_OR:
                    code_obj->emit_jump(source_offset, Bytecode::JumpIfTrue, skip_target);
                    break;
                default:
                    assert(0);
                    break;
                }
                codegen_node(children[1], mode);
                skip_target.resolve();
                break;

            }

            case AstNodeKind::EXPRESSION_CALL:
                codegen_function_call(node_idx, mode);
                break;


            case AstNodeKind::STATEMENT_SEQUENCE:
            case AstNodeKind::STATEMENT_EXPRESSION:
                for(int32_t ch_idx: children)
                {
                    codegen_node(ch_idx, mode);
                }
                break;


            case AstNodeKind::STATEMENT_IF:
            {
                JumpTarget done_target(code_obj);


                for(size_t i = 0; i < children.size()-1; i += 2)
                {
                    JumpTarget next_target(code_obj);
                    codegen_node(children[i+0], mode); // condition, initial check
                    code_obj->emit_jump(source_offset, Bytecode::JumpIfFalse, next_target);
                    codegen_node(children[i+1], mode); //then

                    if(i + 2 != children.size())
                    {
                        // if we have more to emit, we have to generate a jump to the done target. otherwise, we'll just fall through
                        code_obj->emit_jump(source_offset, Bytecode::Jump, done_target);
                    }
                    next_target.resolve();
                }
                if(children.size() & 1) // odd -> else
                {
                    codegen_node(children.back(), mode); //else
                }
                done_target.resolve();

                break;
            }

            case AstNodeKind::STATEMENT_WHILE:
            {
                JumpTarget loop_start_target(code_obj);
                JumpTarget else_target(code_obj);
                JumpTarget break_target(code_obj);
                JumpTarget continue_target(code_obj);
                codegen_node(children[0], mode); // condition, initial check
                code_obj->emit_jump(source_offset, Bytecode::JumpIfFalse, else_target);

                loop_start_target.resolve();

                loop_targets.emplace_back(&break_target, &continue_target);
                codegen_node(children[1], mode); // body
                loop_targets.pop_back();

                continue_target.resolve();
                codegen_node(children[0], mode); // condition, non-initial check
                code_obj->emit_jump(source_offset, Bytecode::JumpIfTrue, loop_start_target);
                else_target.resolve();
                if(children.size() == 3)
                {
                    codegen_node(children[2], mode); // else clause of a loop
                }
                break_target.resolve();
                break;
            }

            case AstNodeKind::STATEMENT_BREAK:
                if(loop_targets.empty())
                {
                    throw std::runtime_error("SyntaxError: 'break' outside loop");
                } else {
                    code_obj->emit_jump(source_offset, Bytecode::Jump, *loop_targets.back().break_target);
                }
                break;

            case AstNodeKind::STATEMENT_CONTINUE:
                if(loop_targets.empty())
                {
                    throw std::runtime_error("SyntaxError: 'continue' not properly in loop");
                } else {
                    code_obj->emit_jump(source_offset, Bytecode::Jump, *loop_targets.back().continue_target);
                }
                break;

            case AstNodeKind::STATEMENT_RETURN:
                if(!children.empty())
                {
                    codegen_node(children[0], mode);
                } else {
                    code_obj->emit_opcode(source_offset, Bytecode::LdaNone);
                }
                code_obj->emit_opcode(source_offset, Bytecode::Return);
                break;

            case AstNodeKind::STATEMENT_PASS:
            case AstNodeKind::STATEMENT_GLOBAL:
            case AstNodeKind::STATEMENT_NONLOCAL:
                break;


            case AstNodeKind::STATEMENT_FUNCTION_DEF:
                codegen_function_definition(node_idx, mode);
                break;

            case AstNodeKind::EXPRESSION_TUPLE:
                throw std::runtime_error("tuple literals not implemented");

            case AstNodeKind::EXPRESSION_LIST:
                throw std::runtime_error("list literals not implemented");

            case AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT:
                throw std::runtime_error("should not end here - this is handled by EXPRESSION_COMPARISON");

            case AstNodeKind::PARAMETER_SEQUENCE:
                throw std::runtime_error("should not end here - this is handled by function definitions");

            }


        }


    };


    CodeObject *generate_code(const AstVector &av)
    {

        return Codegen(av).codegen();

    }

}
