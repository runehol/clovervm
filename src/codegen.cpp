#include "codegen.h"
#include "ast.h"
#include "attr.h"
#include "runtime_helpers.h"
#include "scope.h"
#include "tokenizer.h"
#include <fmt/core.h>
#include <optional>

namespace cl
{

    struct OpTableEntry
    {
        constexpr OpTableEntry(Bytecode _standard = Bytecode::Invalid,
                               Bytecode _binary_acc_smi = Bytecode::Invalid)
            : standard(_standard), binary_acc_smi(_binary_acc_smi)
        {
        }

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

        t.table[size_t(AstOperatorKind::ADD)] =
            OpTableEntry(Bytecode::Add, Bytecode::AddSmi);
        t.table[size_t(AstOperatorKind::SUBTRACT)] =
            OpTableEntry(Bytecode::Sub, Bytecode::SubSmi);
        t.table[size_t(AstOperatorKind::MULTIPLY)] =
            OpTableEntry(Bytecode::Mul, Bytecode::MulSmi);
        t.table[size_t(AstOperatorKind::DIVIDE)] =
            OpTableEntry(Bytecode::Div, Bytecode::DivSmi);
        t.table[size_t(AstOperatorKind::INT_DIVIDE)] =
            OpTableEntry(Bytecode::IntDiv, Bytecode::IntDivSmi);
        t.table[size_t(AstOperatorKind::POWER)] =
            OpTableEntry(Bytecode::Pow, Bytecode::PowSmi);
        t.table[size_t(AstOperatorKind::LEFTSHIFT)] =
            OpTableEntry(Bytecode::LeftShift, Bytecode::LeftShiftSmi);
        t.table[size_t(AstOperatorKind::RIGHTSHIFT)] =
            OpTableEntry(Bytecode::RightShift, Bytecode::RightShiftSmi);
        t.table[size_t(AstOperatorKind::MODULO)] =
            OpTableEntry(Bytecode::Mod, Bytecode::ModSmi);
        t.table[size_t(AstOperatorKind::BITWISE_OR)] =
            OpTableEntry(Bytecode::BitwiseOr, Bytecode::BitwiseOrSmi);
        t.table[size_t(AstOperatorKind::BITWISE_AND)] =
            OpTableEntry(Bytecode::BitwiseAnd, Bytecode::BitwiseAndSmi);
        t.table[size_t(AstOperatorKind::BITWISE_XOR)] =
            OpTableEntry(Bytecode::BitwiseXor, Bytecode::BitwiseXorSmi);

        t.table[size_t(AstOperatorKind::EQUAL)] =
            OpTableEntry(Bytecode::TestEqual);
        t.table[size_t(AstOperatorKind::NOT_EQUAL)] =
            OpTableEntry(Bytecode::TestNotEqual);
        t.table[size_t(AstOperatorKind::LESS)] =
            OpTableEntry(Bytecode::TestLess);
        t.table[size_t(AstOperatorKind::LESS_EQUAL)] =
            OpTableEntry(Bytecode::TestLessEqual);
        t.table[size_t(AstOperatorKind::GREATER)] =
            OpTableEntry(Bytecode::TestGreater);
        t.table[size_t(AstOperatorKind::GREATER_EQUAL)] =
            OpTableEntry(Bytecode::TestGreaterEqual);
        t.table[size_t(AstOperatorKind::IS)] = OpTableEntry(Bytecode::TestIs);
        t.table[size_t(AstOperatorKind::IS_NOT)] =
            OpTableEntry(Bytecode::TestIsNot);
        t.table[size_t(AstOperatorKind::IN)] = OpTableEntry(Bytecode::TestIn);
        t.table[size_t(AstOperatorKind::NOT_IN)] =
            OpTableEntry(Bytecode::TestNotIn);

        t.table[size_t(AstOperatorKind::NOT)] = OpTableEntry(Bytecode::Not);
        t.table[size_t(AstOperatorKind::NEGATE)] =
            OpTableEntry(Bytecode::Negate);
        t.table[size_t(AstOperatorKind::PLUS)] = OpTableEntry(Bytecode::Plus);
        t.table[size_t(AstOperatorKind::BITWISE_NOT)] =
            OpTableEntry(Bytecode::BitwiseNot);

        return t;
    }

    class Codegen
    {
    public:
        using RegisterIndex = int32_t;

        Codegen(const AstVector &_av)
            : av(_av), module_scope(make_internal_raw<Scope>(
                           active_vm()->get_builtin_scope().extract())),
              module_name(interned_string(L"<module>"))

        {
        }

        CodeObject *codegen()
        {
            code_obj = make_code_obj(Mode::Module);
            codegen_node(av.root_node, Mode::Module);
            code_obj->emit_opcode(0, Bytecode::Halt);
            code_obj->finalize(_max_temporary_reg);
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
            Scope *local_scope = nullptr;
            Value name = Value::None();
            switch(mode)
            {
                case Mode::Module:
                    name = module_name;
                    break;

                case Mode::Class:
                    local_scope = make_internal_raw<Scope>(
                        code_obj->local_scope.extract());
                    name = code_obj->name.as_value();
                    break;
                case Mode::Function:
                    local_scope = make_internal_raw<Scope>(
                        code_obj->local_scope.extract());
                    break;
            }

            return make_object_raw<CodeObject>(av.compilation_unit,
                                               module_scope, local_scope, name);
        }

        struct LoopTargetSet
        {
            LoopTargetSet(JumpTarget *_break_target,
                          JumpTarget *_continue_target)
                : break_target(_break_target), continue_target(_continue_target)
            {
            }

            JumpTarget *break_target;
            JumpTarget *continue_target;
        };
        std::vector<LoopTargetSet> loop_targets;

        constexpr static OpTable operator_table = make_table();

        constexpr static OpTableEntry get_operator_entry(AstOperatorKind ok)
        {
            return operator_table.table[size_t(ok)];
        }

        std::optional<int8_t> check_binary_acc_smi_immediate(
            AstOperatorKind op_kind, OpTableEntry entry, int32_t rhs_idx) const
        {
            AstKind rhs_kind = av.kinds[rhs_idx];
            if(entry.binary_acc_smi == Bytecode::Invalid ||
               rhs_kind.node_kind != NumericalConstant.node_kind ||
               rhs_kind.operator_kind != NumericalConstant.operator_kind)
            {
                return std::nullopt;
            }

            Value rhs = av.constants[rhs_idx].as_value();
            if(!rhs.is_smi8())
            {
                return std::nullopt;
            }

            int8_t immediate = rhs.get_smi();
            if(op_kind == AstOperatorKind::LEFTSHIFT &&
               (immediate < 0 || immediate >= 64))
            {
                return std::nullopt;
            }

            return immediate;
        }

        const AstVector &av;
        Scope *module_scope;
        TValue<String> module_name;
        CodeObject *code_obj = nullptr;

        uint32_t _temporary_reg = FrameHeaderSize;
        uint32_t _max_temporary_reg = FrameHeaderSize;

        class TemporaryReg
        {
        public:
            friend class Codegen;
            TemporaryReg(Codegen *_cg, uint32_t _n_regs = 1)
                : cg(_cg), n_regs(_n_regs)
            {
                reg = cg->_temporary_reg;
                cg->_temporary_reg += n_regs;
                cg->_max_temporary_reg =
                    std::max(cg->_max_temporary_reg, cg->_temporary_reg);
            }

            TemporaryReg(const TemporaryReg &) = delete;
            TemporaryReg &operator=(const TemporaryReg &) = delete;

            TemporaryReg(TemporaryReg &&other) noexcept
                : cg(other.cg), n_regs(other.n_regs), reg(other.reg)
            {
                other.cg = nullptr;
                other.n_regs = 0;
                other.reg = 0;
            }

            TemporaryReg &operator=(TemporaryReg &&other) = delete;

            ~TemporaryReg()
            {
                if(cg == nullptr)
                {
                    return;
                }
                cg->_temporary_reg -= n_regs;
                assert(reg == cg->_temporary_reg);
            }

            operator uint32_t() const { return reg; }

        private:
            Codegen *cg;
            uint32_t n_regs;
            uint32_t reg;
        };

        struct ScopedRegister
        {
            RegisterIndex reg;
            std::optional<TemporaryReg> temp;
        };

        static constexpr AstKind NumericalConstant =
            AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NUMBER);

        void reserve_parameter_padding_and_frame_header()
        {
            uint32_t n_parameter_padding =
                code_obj->get_padded_n_parameters() - code_obj->n_parameters;
            code_obj->get_local_scope_ptr()->reserve_empty_slots(
                n_parameter_padding);
            code_obj->get_local_scope_ptr()->reserve_empty_slots(
                FrameHeaderSize);
        }

        ScopedRegister codegen_node_to_register(int32_t node_idx, Mode mode)
        {
            AstKind kind = av.kinds[node_idx];
            if(kind.node_kind == AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                switch(mode)
                {
                    case Mode::Class:
                    case Mode::Function:
                        {
                            int32_t slot_idx =
                                code_obj->get_local_scope_ptr()
                                    ->lookup_slot_index_local(
                                        TValue<String>(av.constants[node_idx]));
                            if(slot_idx >= 0)
                            {
                                return {slot_idx, std::nullopt};
                            }
                            break;
                        }

                    case Mode::Module:
                        break;
                }
            }

            uint32_t source_offset = av.source_offsets[node_idx];
            codegen_node(node_idx, mode);
            std::optional<TemporaryReg> temp;
            temp.emplace(this);
            code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                      RegisterIndex(*temp));
            return {RegisterIndex(*temp), std::move(temp)};
        }

        // used for both regular binary expressions and augmented assignment, so
        // pull out
        void codegen_binary_expression(int32_t node_idx, Mode mode)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            if(immediate.has_value())
            {
                codegen_node(children[0], mode);
                code_obj->emit_opcode_smi(source_offset, entry.binary_acc_smi,
                                          *immediate);
            }
            else
            {
                ScopedRegister lhs_reg =
                    codegen_node_to_register(children[0], mode);
                codegen_node(children[1], mode);
                code_obj->emit_opcode_reg(source_offset, entry.standard,
                                          lhs_reg.reg);
            }
        }

        void codegen_comparison_fragment(int32_t node_idx, Mode mode,
                                         int32_t recv, int32_t prod)
        {
            AstKind kind = av.kinds[node_idx];
            assert(kind.node_kind ==
                   AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT);
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
            // function definitions are involved enough that we prefer a
            // separate function for it
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            uint32_t slot_idx = prepare_variable_assignment(
                TValue<String>(av.constants[node_idx]), mode);

            CodeObject *outer_obj = code_obj;
            CodeObject *fun_obj = make_code_obj(Mode::Function);
            uint32_t outer_temporary_reg = _temporary_reg;
            uint32_t outer_max_temporary_reg = _max_temporary_reg;
            AstChildren param_children = av.children[children[0]];
            {
                code_obj = fun_obj;
                code_obj->name = av.constants[node_idx];
                /*
                  Now we're generating code for the function
                */
                code_obj->n_parameters = param_children.size();
                code_obj->n_positional_parameters =
                    count_positional_parameters(param_children);
                if(has_varargs_parameter(param_children))
                {
                    code_obj->parameter_flags |=
                        FunctionParameterFlags::HasVarArgs;
                }
                for(int32_t ch: param_children)
                {
                    assert(av.kinds[ch].node_kind == AstNodeKind::PARAMETER ||
                           av.kinds[ch].node_kind ==
                               AstNodeKind::PARAMETER_VARARGS);
                    code_obj->get_local_scope_ptr()
                        ->register_slot_index_for_write(
                            TValue<String>(av.constants[ch]));
                }
                reserve_parameter_padding_and_frame_header();

                collect_function_local_bindings(children[1]);

                _temporary_reg = code_obj->get_local_scope_ptr()->size();
                _max_temporary_reg = _temporary_reg;

                // now generate code for the body
                codegen_node(children[1], Mode::Function);
                // finally, emit return None just in case. as a future
                // optimisation, we could check that all return paths already
                // have a return statement
                code_obj->emit_opcode(source_offset, Bytecode::LdaNone);
                code_obj->emit_opcode(source_offset, Bytecode::Return);
                code_obj->finalize(_max_temporary_reg);
            }
            code_obj = outer_obj;
            _temporary_reg = outer_temporary_reg;
            _max_temporary_reg = outer_max_temporary_reg;

            // stick this code object into the constant table, load it, and call
            // the
            uint32_t constant_idx =
                code_obj->allocate_constant(Value::from_oop(fun_obj));
            uint32_t n_defaults = count_default_parameters(param_children);
            if(n_defaults == 0)
            {
                code_obj->emit_opcode_constant_idx(
                    source_offset, Bytecode::CreateFunction, constant_idx);
            }
            else
            {
                TemporaryReg default_values(this, n_defaults);
                size_t first_default_idx =
                    fun_obj->n_positional_parameters - n_defaults;
                for(size_t i = 0; i < n_defaults; ++i)
                {
                    int32_t param_idx = param_children[first_default_idx + i];
                    AstChildren default_children = av.children[param_idx];
                    assert(default_children.size() == 1);
                    codegen_node(default_children[0], mode);
                    code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                              default_values + i);
                }
                code_obj->emit_opcode_reg_range(source_offset,
                                                Bytecode::CreateTuple,
                                                default_values, n_defaults);

                TemporaryReg default_tuple(this);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          default_tuple);
                code_obj->emit_opcode_constant_idx_reg(
                    source_offset, Bytecode::CreateFunctionWithDefaults,
                    constant_idx, default_tuple);
            }

            perform_variable_assignment(source_offset, slot_idx, mode);
        }

        uint32_t count_default_parameters(AstChildren param_children) const
        {
            uint32_t n_defaults = 0;
            for(int32_t param_idx: param_children)
            {
                if(av.kinds[param_idx].node_kind != AstNodeKind::PARAMETER)
                {
                    continue;
                }
                if(!av.children[param_idx].empty())
                {
                    ++n_defaults;
                }
            }
            return n_defaults;
        }

        uint32_t count_positional_parameters(AstChildren param_children) const
        {
            uint32_t n_positional_parameters = 0;
            for(int32_t param_idx: param_children)
            {
                if(av.kinds[param_idx].node_kind ==
                   AstNodeKind::PARAMETER_VARARGS)
                {
                    break;
                }
                ++n_positional_parameters;
            }
            return n_positional_parameters;
        }

        bool has_varargs_parameter(AstChildren param_children) const
        {
            for(int32_t param_idx: param_children)
            {
                if(av.kinds[param_idx].node_kind ==
                   AstNodeKind::PARAMETER_VARARGS)
                {
                    return true;
                }
            }
            return false;
        }

        void codegen_class_definition(int32_t node_idx, Mode mode)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t bases_idx = children[0];
            int32_t body_idx = children[1];

            uint32_t slot_idx = prepare_variable_assignment(
                TValue<String>(av.constants[node_idx]), mode);

            CodeObject *outer_obj = code_obj;
            CodeObject *class_obj = make_code_obj(Mode::Class);
            uint32_t outer_temporary_reg = _temporary_reg;
            uint32_t outer_max_temporary_reg = _max_temporary_reg;
            {
                code_obj = class_obj;
                code_obj->n_parameters = 2;
                code_obj->get_local_scope_ptr()->reserve_empty_slots(2);
                reserve_parameter_padding_and_frame_header();
                collect_class_local_bindings(body_idx);

                _temporary_reg = code_obj->get_local_scope_ptr()->size();
                _max_temporary_reg = _temporary_reg;

                codegen_node(body_idx, Mode::Class);
                code_obj->emit_opcode(source_offset, Bytecode::BuildClass);
                code_obj->finalize(_max_temporary_reg);
            }
            code_obj = outer_obj;
            _temporary_reg = outer_temporary_reg;
            _max_temporary_reg = outer_max_temporary_reg;

            uint32_t body_constant_idx =
                code_obj->allocate_constant(Value::from_oop(class_obj));
            AstChildren bases = av.children[bases_idx];
            uint32_t name_constant_idx =
                code_obj->allocate_constant(av.constants[node_idx]);
            code_obj->emit_opcode_constant_idx(
                source_offset, Bytecode::LdaConstant, name_constant_idx);
            code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                      OutgoingArgReg(0));

            TemporaryReg base_regs(this, std::max<size_t>(bases.size(), 1));
            if(bases.empty())
            {
                uint32_t object_constant_idx = code_obj->allocate_constant(
                    Value::from_oop(active_vm()->object_class()));
                code_obj->emit_opcode_constant_idx(
                    source_offset, Bytecode::LdaConstant, object_constant_idx);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          base_regs);
            }
            else
            {
                for(size_t i = 0; i < bases.size(); ++i)
                {
                    codegen_node(bases[i], mode);
                    code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                              base_regs + i);
                }
            }
            code_obj->emit_opcode_reg_range(source_offset,
                                            Bytecode::CreateTuple, base_regs,
                                            std::max<size_t>(bases.size(), 1));
            code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                      OutgoingArgReg(1));

            code_obj->emit_opcode_constant_idx_reg(
                source_offset, Bytecode::CreateClass, body_constant_idx,
                OutgoingArgReg(0));

            perform_variable_assignment(source_offset, slot_idx, mode);
        }

        void codegen_function_call(int32_t node_idx, Mode mode)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            AstChildren args = av.children[children[1]];

            if(av.kinds[children[0]].node_kind ==
               AstNodeKind::EXPRESSION_ATTRIBUTE)
            {
                AstChildren method_children = av.children[children[0]];
                uint8_t constant_idx =
                    code_obj->allocate_constant(av.constants[children[0]]);
                codegen_node(method_children[0], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          OutgoingArgReg(0));

                for(size_t i = 0; i < args.size(); ++i)
                {
                    codegen_node(args[i], mode);
                    code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                              OutgoingArgReg(1 + i));
                }
                uint8_t read_cache_idx =
                    code_obj->allocate_attribute_read_cache();
                uint8_t call_cache_idx =
                    code_obj->allocate_function_call_cache();
                code_obj->emit_opcode_reg_constant_idx_cache_idx_argc(
                    source_offset, Bytecode::CallMethodAttr, OutgoingArgReg(0),
                    constant_idx, read_cache_idx, call_cache_idx, args.size());
                return;
            }

            // function itself
            TemporaryReg callable_reg(this);
            codegen_node(children[0], mode);
            code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                      callable_reg);

            for(size_t i = 0; i < args.size(); ++i)
            {
                codegen_node(args[i], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          OutgoingArgReg(i));
            }
            code_obj->emit_call_simple(source_offset, callable_reg,
                                       OutgoingArgReg(0), args.size());
        }

        void codegen_list_literal(int32_t node_idx, Mode mode)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(this, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                codegen_node(children[i], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          regs + i);
            }
            code_obj->emit_opcode_reg_range(source_offset, Bytecode::CreateList,
                                            regs, children.size());
        }

        void codegen_tuple_literal(int32_t node_idx, Mode mode)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(this, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                codegen_node(children[i], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          regs + i);
            }
            code_obj->emit_opcode_reg_range(
                source_offset, Bytecode::CreateTuple, regs, children.size());
        }

        void codegen_dict_literal(int32_t node_idx, Mode mode)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];

            TemporaryReg regs(this, std::max<size_t>(children.size(), 1));
            for(size_t i = 0; i < children.size(); ++i)
            {
                codegen_node(children[i], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          regs + i);
            }
            code_obj->emit_opcode_reg_range(source_offset, Bytecode::CreateDict,
                                            regs, children.size() / 2);
        }

        void codegen_subscript_assignment(int32_t node_idx, Mode mode)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t lhs_idx = children[0];
            AstChildren lhs_children = av.children[lhs_idx];
            ScopedRegister receiver_reg =
                codegen_node_to_register(lhs_children[0], mode);
            ScopedRegister key_reg =
                codegen_node_to_register(lhs_children[1], mode);

            if(kind.operator_kind == AstOperatorKind::NOP)
            {
                codegen_node(children[1], mode);
                code_obj->emit_opcode_reg_reg(source_offset,
                                              Bytecode::StoreSubscript,
                                              receiver_reg.reg, key_reg.reg);
                return;
            }

            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            code_obj->emit_opcode_reg(source_offset, Bytecode::Ldar,
                                      key_reg.reg);
            code_obj->emit_opcode_reg(source_offset, Bytecode::LoadSubscript,
                                      receiver_reg.reg);

            if(immediate.has_value())
            {
                code_obj->emit_opcode_smi(source_offset, entry.binary_acc_smi,
                                          *immediate);
            }
            else
            {
                TemporaryReg lhs_value_reg(this);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          lhs_value_reg);
                codegen_node(children[1], mode);
                code_obj->emit_opcode_reg(source_offset, entry.standard,
                                          lhs_value_reg);
            }

            code_obj->emit_opcode_reg_reg(source_offset,
                                          Bytecode::StoreSubscript,
                                          receiver_reg.reg, key_reg.reg);
        }

        void codegen_attribute_assignment(int32_t node_idx, Mode mode)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t lhs_idx = children[0];
            AstChildren lhs_children = av.children[lhs_idx];
            uint8_t constant_idx =
                code_obj->allocate_constant(av.constants[lhs_idx]);
            ScopedRegister receiver_reg =
                codegen_node_to_register(lhs_children[0], mode);

            if(kind.operator_kind == AstOperatorKind::NOP)
            {
                codegen_node(children[1], mode);
                uint8_t cache_idx =
                    code_obj->allocate_attribute_mutation_cache();
                code_obj->emit_opcode_reg_constant_idx_cache_idx(
                    source_offset, Bytecode::StoreAttr, receiver_reg.reg,
                    constant_idx, cache_idx);
                return;
            }

            OpTableEntry entry = get_operator_entry(kind.operator_kind);
            std::optional<int8_t> immediate = check_binary_acc_smi_immediate(
                kind.operator_kind, entry, children[1]);

            uint8_t load_cache_idx = code_obj->allocate_attribute_read_cache();
            code_obj->emit_opcode_reg_constant_idx_cache_idx(
                source_offset, Bytecode::LoadAttr, receiver_reg.reg,
                constant_idx, load_cache_idx);

            if(immediate.has_value())
            {
                code_obj->emit_opcode_smi(source_offset, entry.binary_acc_smi,
                                          *immediate);
            }
            else
            {
                TemporaryReg lhs_value_reg(this);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          lhs_value_reg);
                codegen_node(children[1], mode);
                code_obj->emit_opcode_reg(source_offset, entry.standard,
                                          lhs_value_reg);
            }

            uint8_t store_cache_idx =
                code_obj->allocate_attribute_mutation_cache();
            code_obj->emit_opcode_reg_constant_idx_cache_idx(
                source_offset, Bytecode::StoreAttr, receiver_reg.reg,
                constant_idx, store_cache_idx);
        }

        std::optional<uint8_t> direct_range_call_arity(int32_t node_idx) const
        {
            if(av.kinds[node_idx].node_kind != AstNodeKind::EXPRESSION_CALL)
            {
                return std::nullopt;
            }

            AstChildren children = av.children[node_idx];
            int32_t callable_idx = children[0];
            if(av.kinds[callable_idx].node_kind !=
               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
            {
                return std::nullopt;
            }

            TValue<String> range_name = interned_string(L"range");
            if(av.constants[callable_idx] != Value(range_name))
            {
                return std::nullopt;
            }

            size_t n_args = av.children[children[1]].size();
            if(n_args < 1 || n_args > 3)
            {
                return std::nullopt;
            }

            return uint8_t(n_args);
        }

        void codegen_loop_body(int32_t body_idx, Mode mode,
                               JumpTarget &break_target,
                               JumpTarget &continue_target)
        {
            loop_targets.emplace_back(&break_target, &continue_target);
            codegen_node(body_idx, mode);
            loop_targets.pop_back();
        }

        void codegen_iterator_driven_for_loop(uint32_t source_offset,
                                              uint32_t target_slot,
                                              int32_t body_idx, Mode mode,
                                              uint32_t iterator_reg,
                                              JumpTarget &else_target,
                                              JumpTarget &break_target)
        {
            JumpTarget loop_start_target(code_obj);
            JumpTarget continue_target(code_obj);

            loop_start_target.resolve();
            code_obj->emit_opcode_reg_jump(source_offset, Bytecode::ForIter,
                                           iterator_reg, else_target);
            perform_variable_assignment(source_offset, target_slot, mode);

            codegen_loop_body(body_idx, mode, break_target, continue_target);

            continue_target.resolve();
            code_obj->emit_jump(source_offset, Bytecode::Jump,
                                loop_start_target);
        }

        void codegen_direct_range_for_loop(int32_t node_idx,
                                           uint32_t target_slot, uint8_t n_args,
                                           Mode mode)
        {
            AstChildren children = av.children[node_idx];
            uint32_t source_offset = av.source_offsets[node_idx];
            int32_t iterable_idx = children[1];
            int32_t body_idx = children[2];
            int32_t else_idx = children.size() == 4 ? children[3] : -1;
            AstChildren call_children = av.children[iterable_idx];
            AstChildren args = av.children[call_children[1]];

            TemporaryReg range_regs(this, 1 + n_args);
            TemporaryReg iterator_reg(this);
            JumpTarget generic_fallback_target(code_obj);
            JumpTarget fast_loop_start_target(code_obj);
            JumpTarget fast_continue_target(code_obj);
            JumpTarget else_target(code_obj);
            JumpTarget break_target(code_obj);

            codegen_node(call_children[0], mode);
            code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                      range_regs + 0);
            for(size_t i = 0; i < args.size(); ++i)
            {
                codegen_node(args[i], mode);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          range_regs + 1 + i);
            }

            Bytecode prep_opcode = Bytecode::Invalid;
            Bytecode iter_opcode = Bytecode::Invalid;
            switch(n_args)
            {
                case 1:
                    prep_opcode = Bytecode::ForPrepRange1;
                    iter_opcode = Bytecode::ForIterRange1;
                    break;
                case 2:
                    prep_opcode = Bytecode::ForPrepRange2;
                    iter_opcode = Bytecode::ForIterRange1;
                    break;
                case 3:
                    prep_opcode = Bytecode::ForPrepRange3;
                    iter_opcode = Bytecode::ForIterRangeStep;
                    break;
                default:
                    assert(false);
            }

            code_obj->emit_opcode_reg_jump(source_offset, prep_opcode,
                                           range_regs, generic_fallback_target);

            fast_loop_start_target.resolve();
            code_obj->emit_opcode_reg_jump(source_offset, iter_opcode,
                                           range_regs, else_target);
            perform_variable_assignment(source_offset, target_slot, mode);
            codegen_loop_body(body_idx, mode, break_target,
                              fast_continue_target);
            fast_continue_target.resolve();
            code_obj->emit_jump(source_offset, Bytecode::Jump,
                                fast_loop_start_target);

            generic_fallback_target.resolve();
            for(size_t i = 0; i < args.size(); ++i)
            {
                code_obj->emit_opcode_reg(source_offset, Bytecode::Ldar,
                                          range_regs + 1 + i);
                code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                          OutgoingArgReg(i));
            }
            code_obj->emit_call_simple(source_offset, range_regs,
                                       OutgoingArgReg(0), n_args);
            code_obj->emit_opcode(source_offset, Bytecode::GetIter);
            code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                      iterator_reg);
            codegen_iterator_driven_for_loop(source_offset, target_slot,
                                             body_idx, mode, iterator_reg,
                                             else_target, break_target);

            else_target.resolve();
            if(else_idx >= 0)
            {
                codegen_node(else_idx, mode);
            }
            break_target.resolve();
        }

        uint32_t prepare_variable_assignment(TValue<String> var_name, Mode mode)
        {
            switch(mode)
            {
                case Mode::Module:
                    return code_obj->module_scope.extract()
                        ->register_slot_index_for_write(var_name);

                case Mode::Class:
                case Mode::Function:
                    return code_obj->get_local_scope_ptr()
                        ->register_slot_index_for_write(var_name);
            }
        }

        void perform_variable_assignment(uint32_t source_offset,
                                         uint32_t slot_idx, Mode mode)
        {
            switch(mode)
            {
                case Mode::Module:
                    code_obj->emit_opcode_uint32(source_offset,
                                                 Bytecode::StaGlobal, slot_idx);
                    break;
                case Mode::Class:
                case Mode::Function:
                    code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                              slot_idx);
                    break;
            }
        }

        void collect_function_local_bindings(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];

            switch(kind.node_kind)
            {
                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                case AstNodeKind::STATEMENT_CLASS_DEF:
                    code_obj->get_local_scope_ptr()
                        ->register_slot_index_for_write(
                            TValue<String>(av.constants[node_idx]));
                    return;

                case AstNodeKind::STATEMENT_ASSIGN:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    {
                        int32_t lhs_idx = children[0];
                        if(av.kinds[lhs_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            code_obj->get_local_scope_ptr()
                                ->register_slot_index_for_write(
                                    TValue<String>(av.constants[lhs_idx]));
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_FOR:
                    code_obj->get_local_scope_ptr()
                        ->register_slot_index_for_write(
                            TValue<String>(av.constants[children[0]]));
                    break;

                default:
                    break;
            }

            for(int32_t child_idx: children)
            {
                collect_function_local_bindings(child_idx);
            }
        }

        void collect_class_local_bindings(int32_t node_idx)
        {
            AstKind kind = av.kinds[node_idx];
            AstChildren children = av.children[node_idx];

            switch(kind.node_kind)
            {
                case AstNodeKind::STATEMENT_FUNCTION_DEF:
                case AstNodeKind::STATEMENT_CLASS_DEF:
                    code_obj->get_local_scope_ptr()
                        ->register_slot_index_for_write(
                            TValue<String>(av.constants[node_idx]));
                    return;

                case AstNodeKind::STATEMENT_ASSIGN:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    {
                        int32_t lhs_idx = children[0];
                        if(av.kinds[lhs_idx].node_kind ==
                           AstNodeKind::EXPRESSION_VARIABLE_REFERENCE)
                        {
                            code_obj->get_local_scope_ptr()
                                ->register_slot_index_for_write(
                                    TValue<String>(av.constants[lhs_idx]));
                        }
                        break;
                    }

                case AstNodeKind::STATEMENT_FOR:
                    code_obj->get_local_scope_ptr()
                        ->register_slot_index_for_write(
                            TValue<String>(av.constants[children[0]]));
                    break;

                default:
                    break;
            }

            for(int32_t child_idx: children)
            {
                collect_class_local_bindings(child_idx);
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
                                int32_t slot_idx =
                                    code_obj->get_local_scope_ptr()
                                        ->lookup_slot_index_local(
                                            TValue<String>(
                                                av.constants[node_idx]));
                                if(slot_idx >= 0)
                                {
                                    code_obj->emit_opcode_reg(source_offset,
                                                              Bytecode::Ldar,
                                                              slot_idx);
                                    break;
                                }
                                /* otherwise, fallthrough */
                            }
                        case Mode::Module:
                            {
                                uint32_t slot_idx =
                                    code_obj->module_scope.extract()
                                        ->register_slot_index_for_read(
                                            TValue<String>(
                                                av.constants[node_idx]));
                                code_obj->emit_opcode_uint32(
                                    source_offset, Bytecode::LdaGlobal,
                                    slot_idx);
                                break;
                            }
                    }
                    break;

                case AstNodeKind::STATEMENT_ASSIGN:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    {

                        int32_t lhs_idx = children[0];
                        AstNodeKind lhs_kind = av.kinds[lhs_idx].node_kind;
                        if(lhs_kind !=
                               AstNodeKind::EXPRESSION_VARIABLE_REFERENCE &&
                           lhs_kind != AstNodeKind::EXPRESSION_ATTRIBUTE &&
                           !(lhs_kind == AstNodeKind::EXPRESSION_BINARY &&
                             av.kinds[lhs_idx].operator_kind ==
                                 AstOperatorKind::SUBSCRIPT))
                        {
                            throw std::runtime_error(
                                "We don't support assignment to anything but "
                                "simple variables, attributes, and subscripts "
                                "yet");
                        }

                        if(lhs_kind == AstNodeKind::EXPRESSION_ATTRIBUTE)
                        {
                            codegen_attribute_assignment(node_idx, mode);
                            break;
                        }
                        if(lhs_kind == AstNodeKind::EXPRESSION_BINARY)
                        {
                            codegen_subscript_assignment(node_idx, mode);
                            break;
                        }

                        uint32_t slot_idx = prepare_variable_assignment(
                            TValue<String>(av.constants[lhs_idx]), mode);

                        // augmented assignment
                        if(kind.operator_kind != AstOperatorKind::NOP)
                        {
                            codegen_binary_expression(node_idx, mode);
                        }
                        else
                        {
                            // just compute the RHS
                            codegen_node(children[1], mode);
                        }
                        perform_variable_assignment(source_offset, slot_idx,
                                                    mode);
                        break;
                    }

                case AstNodeKind::EXPRESSION_BINARY:
                    if(kind.operator_kind == AstOperatorKind::SUBSCRIPT)
                    {
                        ScopedRegister receiver_reg =
                            codegen_node_to_register(children[0], mode);
                        codegen_node(children[1], mode);
                        code_obj->emit_opcode_reg(source_offset,
                                                  Bytecode::LoadSubscript,
                                                  receiver_reg.reg);
                        break;
                    }
                    codegen_binary_expression(node_idx, mode);
                    break;

                case AstNodeKind::EXPRESSION_UNARY:
                    {
                        OpTableEntry entry =
                            get_operator_entry(kind.operator_kind);
                        codegen_node(children[0], mode);
                        code_obj->emit_opcode(source_offset, entry.standard);
                        break;
                    }
                case AstNodeKind::EXPRESSION_LITERAL:
                    {

                        switch(kind.operator_kind)
                        {
                            case AstOperatorKind::NONE:
                                code_obj->emit_opcode(source_offset,
                                                      Bytecode::LdaNone);
                                break;
                            case AstOperatorKind::TRUE:
                                code_obj->emit_opcode(source_offset,
                                                      Bytecode::LdaTrue);
                                break;
                            case AstOperatorKind::FALSE:
                                code_obj->emit_opcode(source_offset,
                                                      Bytecode::LdaFalse);
                                break;

                            case AstOperatorKind::NUMBER:
                                {
                                    Value val =
                                        av.constants[node_idx].as_value();
                                    if(val.is_smi8())
                                    {
                                        code_obj->emit_opcode_smi(
                                            source_offset, Bytecode::LdaSmi,
                                            val.get_smi());
                                    }
                                    else
                                    {
                                        uint32_t constant_idx =
                                            code_obj->allocate_constant(val);
                                        code_obj->emit_opcode_constant_idx(
                                            source_offset,
                                            Bytecode::LdaConstant,
                                            constant_idx);
                                        break;
                                    }
                                    break;
                                }

                            case AstOperatorKind::STRING:
                                {
                                    uint32_t constant_idx =
                                        code_obj->allocate_constant(
                                            av.constants[node_idx]);
                                    code_obj->emit_opcode_constant_idx(
                                        source_offset, Bytecode::LdaConstant,
                                        constant_idx);
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

                        ScopedRegister recv_reg =
                            codegen_node_to_register(children[0], mode);
                        TemporaryReg prod_reg(this);
                        int32_t recv = recv_reg.reg;
                        int32_t prod = prod_reg;
                        for(size_t i = 1; i < children.size(); ++i)
                        {
                            bool last = i == children.size() - 1;
                            if(last)
                                prod = -1;

                            codegen_comparison_fragment(children[i], mode, recv,
                                                        prod);

                            if(!last)
                            {
                                code_obj->emit_jump(source_offset,
                                                    Bytecode::JumpIfFalse,
                                                    skip_target);
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
                                code_obj->emit_jump(source_offset,
                                                    Bytecode::JumpIfFalse,
                                                    skip_target);
                                break;
                            case AstOperatorKind::SHORTCUTTING_OR:
                                code_obj->emit_jump(source_offset,
                                                    Bytecode::JumpIfTrue,
                                                    skip_target);
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

                case AstNodeKind::EXPRESSION_ATTRIBUTE:
                    {
                        ScopedRegister receiver_reg =
                            codegen_node_to_register(children[0], mode);
                        uint8_t constant_idx =
                            code_obj->allocate_constant(av.constants[node_idx]);
                        uint8_t cache_idx =
                            code_obj->allocate_attribute_read_cache();
                        code_obj->emit_opcode_reg_constant_idx_cache_idx(
                            source_offset, Bytecode::LoadAttr, receiver_reg.reg,
                            constant_idx, cache_idx);
                        break;
                    }

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

                        for(size_t i = 0; i < children.size() - 1; i += 2)
                        {
                            JumpTarget next_target(code_obj);
                            codegen_node(children[i + 0],
                                         mode);  // condition, initial check
                            code_obj->emit_jump(source_offset,
                                                Bytecode::JumpIfFalse,
                                                next_target);
                            codegen_node(children[i + 1], mode);  // then

                            if(i + 2 != children.size())
                            {
                                // if we have more to emit, we have to generate
                                // a jump to the done target. otherwise, we'll
                                // just fall through
                                code_obj->emit_jump(
                                    source_offset, Bytecode::Jump, done_target);
                            }
                            next_target.resolve();
                        }
                        if(children.size() & 1)  // odd -> else
                        {
                            codegen_node(children.back(), mode);  // else
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
                        codegen_node(children[0],
                                     mode);  // condition, initial check
                        code_obj->emit_jump(source_offset,
                                            Bytecode::JumpIfFalse, else_target);

                        loop_start_target.resolve();

                        loop_targets.emplace_back(&break_target,
                                                  &continue_target);
                        codegen_node(children[1], mode);  // body
                        loop_targets.pop_back();

                        continue_target.resolve();
                        codegen_node(children[0],
                                     mode);  // condition, non-initial check
                        code_obj->emit_jump(source_offset, Bytecode::JumpIfTrue,
                                            loop_start_target);
                        else_target.resolve();
                        if(children.size() == 3)
                        {
                            codegen_node(children[2],
                                         mode);  // else clause of a loop
                        }
                        break_target.resolve();
                        break;
                    }

                case AstNodeKind::STATEMENT_FOR:
                    {
                        int32_t target_idx = children[0];
                        uint32_t target_slot = prepare_variable_assignment(
                            TValue<String>(av.constants[target_idx]), mode);
                        std::optional<uint8_t> range_call_arity =
                            direct_range_call_arity(children[1]);
                        if(range_call_arity.has_value())
                        {
                            codegen_direct_range_for_loop(
                                node_idx, target_slot, *range_call_arity, mode);
                            break;
                        }

                        int32_t iterable_idx = children[1];
                        int32_t body_idx = children[2];
                        int32_t else_idx =
                            children.size() == 4 ? children[3] : -1;
                        TemporaryReg iterator_reg(this);
                        JumpTarget else_target(code_obj);
                        JumpTarget break_target(code_obj);

                        codegen_node(iterable_idx, mode);
                        code_obj->emit_opcode(source_offset, Bytecode::GetIter);
                        code_obj->emit_opcode_reg(source_offset, Bytecode::Star,
                                                  iterator_reg);
                        codegen_iterator_driven_for_loop(
                            source_offset, target_slot, body_idx, mode,
                            iterator_reg, else_target, break_target);
                        else_target.resolve();
                        if(else_idx >= 0)
                        {
                            codegen_node(else_idx, mode);
                        }
                        break_target.resolve();
                        break;
                    }

                case AstNodeKind::STATEMENT_BREAK:
                    if(loop_targets.empty())
                    {
                        throw std::runtime_error(
                            "SyntaxError: 'break' outside loop");
                    }
                    else
                    {
                        code_obj->emit_jump(source_offset, Bytecode::Jump,
                                            *loop_targets.back().break_target);
                    }
                    break;

                case AstNodeKind::STATEMENT_CONTINUE:
                    if(loop_targets.empty())
                    {
                        throw std::runtime_error(
                            "SyntaxError: 'continue' not properly in loop");
                    }
                    else
                    {
                        code_obj->emit_jump(
                            source_offset, Bytecode::Jump,
                            *loop_targets.back().continue_target);
                    }
                    break;

                case AstNodeKind::STATEMENT_RETURN:
                    if(mode != Mode::Function)
                    {
                        throw std::runtime_error(
                            "SyntaxError: 'return' outside function");
                    }
                    if(!children.empty())
                    {
                        codegen_node(children[0], mode);
                    }
                    else
                    {
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

                case AstNodeKind::STATEMENT_CLASS_DEF:
                    codegen_class_definition(node_idx, mode);
                    break;

                case AstNodeKind::EXPRESSION_TUPLE:
                    codegen_tuple_literal(node_idx, mode);
                    break;

                case AstNodeKind::EXPRESSION_LIST:
                    codegen_list_literal(node_idx, mode);
                    break;

                case AstNodeKind::EXPRESSION_DICT:
                    codegen_dict_literal(node_idx, mode);
                    break;

                case AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT:
                    throw std::runtime_error(
                        "should not end here - this is handled by "
                        "EXPRESSION_COMPARISON");

                case AstNodeKind::PARAMETER_SEQUENCE:
                case AstNodeKind::PARAMETER:
                case AstNodeKind::PARAMETER_VARARGS:
                    throw std::runtime_error("should not end here - this is "
                                             "handled by function definitions");
            }
        }
    };

    CodeObject *generate_code(const AstVector &av)
    {

        return Codegen(av).codegen();
    }

}  // namespace cl
