#ifndef CL_CODE_OBJECT_BUILDER_H
#define CL_CODE_OBJECT_BUILDER_H

#include "code_object.h"
#include <cassert>
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

        void add_relocation(uint32_t pos);

    private:
        void resolve_relocation(uint32_t pos);

        CodeObjectBuilder *builder;
        int32_t target;
        std::vector<uint32_t> unresolved_relocations;
    };

    class CodeObjectBuilder
    {
    public:
        class TemporaryReg
        {
        public:
            TemporaryReg(CodeObjectBuilder &_builder, uint32_t _n_regs = 1);

            TemporaryReg(const TemporaryReg &) = delete;
            TemporaryReg &operator=(const TemporaryReg &) = delete;

            TemporaryReg(TemporaryReg &&other) noexcept;
            TemporaryReg &operator=(TemporaryReg &&other) = delete;

            ~TemporaryReg();

            operator uint32_t() const { return reg; }

        private:
            CodeObjectBuilder *builder;
            uint32_t n_regs;
            uint32_t reg;
        };

        CodeObjectBuilder(const CompilationUnit *compilation_unit,
                          Scope *module_scope, Scope *local_scope, Value name);
        CodeObjectBuilder(VirtualMachine *vm,
                          const CompilationUnit *compilation_unit,
                          Scope *module_scope, Scope *local_scope, Value name);

        CodeObjectBuilder(const CodeObjectBuilder &) = delete;
        CodeObjectBuilder &operator=(const CodeObjectBuilder &) = delete;

        CodeObjectBuilder(CodeObjectBuilder &&other) noexcept
            : code_obj(other.code_obj), finalized(other.finalized),
              temporary_reg(other.temporary_reg),
              max_temporary_reg(other.max_temporary_reg),
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

        uint32_t emit_clear_local(uint32_t source_offset, uint32_t reg);
        uint32_t emit_ldar(uint32_t source_offset, uint32_t reg);
        uint32_t emit_load_local_checked(uint32_t source_offset, uint32_t reg);
        uint32_t emit_lda_global(uint32_t source_offset, uint32_t slot_idx);
        uint32_t emit_star(uint32_t source_offset, uint32_t reg);
        uint32_t emit_star(uint32_t source_offset, OutgoingArgReg reg);
        uint32_t emit_sta_global(uint32_t source_offset, uint32_t slot_idx);
        uint32_t emit_del_local(uint32_t source_offset, uint32_t reg);
        uint32_t emit_del_global(uint32_t source_offset, uint32_t slot_idx);
        uint32_t emit_lda_none(uint32_t source_offset);
        uint32_t emit_lda_true(uint32_t source_offset);
        uint32_t emit_lda_false(uint32_t source_offset);
        uint32_t emit_lda_smi(uint32_t source_offset, int8_t smi);
        uint32_t emit_lda_constant(uint32_t source_offset,
                                   uint8_t constant_idx);
        uint32_t emit_return(uint32_t source_offset);
        uint32_t emit_return_or_raise_exception(uint32_t source_offset);
        uint32_t emit_raise_if_unhandled_exception(uint32_t source_offset);
        uint32_t emit_halt(uint32_t source_offset);
        uint32_t emit_get_iter(uint32_t source_offset);
        uint32_t emit_build_class(uint32_t source_offset);
        uint32_t emit_check_init_returned_none(uint32_t source_offset);
        uint32_t emit_assert(uint32_t source_offset);
        uint32_t emit_create_instance_known_class(uint32_t source_offset,
                                                  uint8_t class_idx);
        uint32_t emit_create_function(uint32_t source_offset, uint8_t code_idx);
        uint32_t emit_create_function_with_defaults(uint32_t source_offset,
                                                    uint8_t code_idx,
                                                    uint32_t defaults_reg);
        uint32_t emit_create_tuple(uint32_t source_offset, uint32_t first_reg,
                                   uint8_t n_regs);
        uint32_t emit_create_tuple(uint32_t source_offset, OutgoingArgReg reg,
                                   uint8_t n_regs);
        uint32_t emit_create_list(uint32_t source_offset, uint32_t first_reg,
                                  uint8_t n_regs);
        uint32_t emit_create_dict(uint32_t source_offset, uint32_t first_reg,
                                  uint8_t n_entries);
        uint32_t emit_create_class(uint32_t source_offset,
                                   uint8_t body_constant_idx,
                                   OutgoingArgReg first_arg_reg);
        uint32_t emit_load_attr(uint32_t source_offset, uint32_t receiver_reg,
                                uint8_t name_idx);
        uint32_t emit_store_attr(uint32_t source_offset, uint32_t receiver_reg,
                                 uint8_t name_idx);
        uint32_t emit_del_attr(uint32_t source_offset, uint32_t receiver_reg,
                               uint8_t name_idx);
        uint32_t emit_call_method_attr(uint32_t source_offset,
                                       OutgoingArgReg first_arg_reg,
                                       uint8_t name_idx, uint8_t argc);
        uint32_t emit_load_subscript(uint32_t source_offset,
                                     uint32_t receiver_reg);
        uint32_t emit_store_subscript(uint32_t source_offset,
                                      uint32_t receiver_reg, uint32_t key_reg);
        uint32_t emit_del_subscript(uint32_t source_offset,
                                    uint32_t receiver_reg, uint32_t key_reg);
        uint32_t emit_jump(uint32_t source_offset, JumpTarget &target);
        uint32_t emit_jump_if_false(uint32_t source_offset, JumpTarget &target);
        uint32_t emit_jump_if_true(uint32_t source_offset, JumpTarget &target);
        uint32_t emit_for_iter(uint32_t source_offset, uint32_t iterator_reg,
                               JumpTarget &target);
        uint32_t emit_for_prep_range(uint32_t source_offset, Bytecode op,
                                     uint32_t range_regs, JumpTarget &target);
        uint32_t emit_for_iter_range(uint32_t source_offset, Bytecode op,
                                     uint32_t range_regs, JumpTarget &target);
        uint32_t emit_binary_op(uint32_t source_offset, Bytecode op,
                                uint32_t lhs_reg);
        uint32_t emit_binary_smi_op(uint32_t source_offset, Bytecode op,
                                    int8_t rhs);
        uint32_t emit_compare_op(uint32_t source_offset, Bytecode op,
                                 uint32_t lhs_reg);
        uint32_t emit_unary_op(uint32_t source_offset, Bytecode op);
        uint32_t emit_call_code_object(uint32_t source_offset,
                                       uint8_t code_object_idx,
                                       OutgoingArgReg first_arg_reg,
                                       uint8_t argc);
        uint32_t emit_call_native(uint32_t source_offset, Bytecode op,
                                  uint8_t target_idx);
        uint32_t emit_call_simple(uint32_t source_offset, uint32_t callable_reg,
                                  OutgoingArgReg first_arg_reg, uint8_t argc);

        uint32_t allocate_constant(Value val);
        uint32_t add_native_function_target(NativeFunctionTarget target);

        CodeObject *finalize();

    private:
        friend class JumpTarget;

        uint32_t allocate_attribute_read_cache();
        uint32_t allocate_attribute_mutation_cache();
        uint32_t allocate_function_call_cache();
        uint32_t emplace_back(uint32_t source_offset, uint8_t c);
        uint32_t emit_opcode(uint32_t source_offset, Bytecode c);
        uint32_t emit_opcode_smi(uint32_t source_offset, Bytecode c,
                                 int8_t smi);
        uint32_t emit_opcode_constant_idx(uint32_t source_offset, Bytecode c,
                                          uint8_t constant_idx);
        uint32_t emit_opcode_constant_idx_reg(uint32_t source_offset,
                                              Bytecode c, uint8_t constant_idx,
                                              uint32_t reg);
        uint32_t emit_opcode_constant_idx_reg(uint32_t source_offset,
                                              Bytecode c, uint8_t constant_idx,
                                              OutgoingArgReg reg);
        uint32_t emit_opcode_constant_idx_reg_argc(uint32_t source_offset,
                                                   Bytecode c,
                                                   uint8_t constant_idx,
                                                   OutgoingArgReg reg,
                                                   uint8_t argc);
        uint32_t emit_opcode_reg(uint32_t source_offset, Bytecode c,
                                 uint32_t reg);
        uint32_t emit_opcode_reg(uint32_t source_offset, Bytecode c,
                                 OutgoingArgReg reg);
        uint32_t emit_opcode_reg_range(uint32_t source_offset, Bytecode c,
                                       uint32_t reg, uint8_t n_regs);
        uint32_t emit_opcode_reg_range(uint32_t source_offset, Bytecode c,
                                       OutgoingArgReg reg, uint8_t n_regs);
        uint32_t emit_opcode_native_target_idx(uint32_t source_offset,
                                               Bytecode c, uint8_t target_idx);
        uint32_t emit_opcode_reg_constant_idx_cache_idx(uint32_t source_offset,
                                                        Bytecode c,
                                                        uint32_t reg,
                                                        uint8_t constant_idx,
                                                        uint8_t cache_idx);
        uint32_t emit_opcode_reg_constant_idx_cache_idx_argc(
            uint32_t source_offset, Bytecode c, OutgoingArgReg reg,
            uint8_t constant_idx, uint8_t read_cache_idx,
            uint8_t call_cache_idx, uint8_t argc);
        uint32_t emit_opcode_reg_reg(uint32_t source_offset, Bytecode c,
                                     uint32_t first_reg, uint32_t second_reg);
        uint32_t emit_opcode_reg_jump(uint32_t source_offset, Bytecode c,
                                      uint32_t reg, JumpTarget &target);
        uint32_t emit_opcode_uint32(uint32_t source_offset, Bytecode c,
                                    uint32_t k);
        uint32_t emit_jump(uint32_t source_offset, Bytecode c,
                           JumpTarget &target);
        void set_int16(uint32_t pos, int16_t v);
        void set_encoded_reg(uint32_t pos, uint32_t reg);
        void assert_not_finalized() const;
        void patch_outgoing_arg_relocations();
        void add_outgoing_arg_relocation(uint32_t operand_offset,
                                         uint32_t outgoing_slot_offset);
        uint32_t first_temporary_reg() const;
        void sync_temporary_reg_base();
        uint32_t reserve_registers(uint32_t n_regs);
        void release_registers(uint32_t reg, uint32_t n_regs);

        CodeObject *code_obj;
        bool finalized = false;
        uint32_t temporary_reg = FrameHeaderSize;
        uint32_t max_temporary_reg = FrameHeaderSize;
        struct OutgoingArgRelocation
        {
            uint32_t operand_offset;
        };

        std::vector<OutgoingArgRelocation> outgoing_arg_relocations;
    };

}  // namespace cl

#endif  // CL_CODE_OBJECT_BUILDER_H
