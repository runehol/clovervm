#ifndef CL_CODE_OBJECT_BUILDER_H
#define CL_CODE_OBJECT_BUILDER_H

#include "code_object.h"
#include "typed_value.h"
#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cl
{
    class CodeObjectBuilder;
    class ExceptionTableRangeSuspension;

    enum class JumpRelocationKind : uint8_t
    {
        BytecodeRelativeI16,
        ExceptionTableStartAbsoluteU32,
        ExceptionTableEndAbsoluteU32,
        ExceptionTableHandlerAbsoluteU32,
    };

    struct JumpRelocation
    {
        JumpRelocationKind kind;
        uint32_t index;
    };

    enum class RegisterAlignment : uint8_t
    {
        None,
        CallFrame,
    };

    class JumpTarget
    {
    public:
        JumpTarget(CodeObjectBuilder *_builder) : builder(_builder), target(-1)
        {
        }

        JumpTarget(const JumpTarget &) = delete;
        JumpTarget &operator=(const JumpTarget &) = delete;

        ~JumpTarget();

        Expected<void> resolve();

        Expected<void> add_relocation(uint32_t pos);
        Expected<void>
        add_bytecode_relative_i16_relocation(uint32_t operand_offset);
        Expected<void>
        add_exception_table_start_absolute_u32_relocation(uint32_t entry_idx);
        Expected<void>
        add_exception_table_end_absolute_u32_relocation(uint32_t entry_idx);
        Expected<void>
        add_exception_table_handler_absolute_u32_relocation(uint32_t entry_idx);

    private:
        Expected<void> add_relocation(JumpRelocation relocation);
        Expected<void> resolve_relocation(JumpRelocation relocation);

        CodeObjectBuilder *builder;
        int32_t target;
        std::vector<JumpRelocation> unresolved_relocations;
    };

    class ExceptionTableRangeBuilder
    {
    public:
        ExceptionTableRangeBuilder(CodeObjectBuilder *_builder,
                                   JumpTarget &_handler_target);

        ExceptionTableRangeBuilder(const ExceptionTableRangeBuilder &) = delete;
        ExceptionTableRangeBuilder &
        operator=(const ExceptionTableRangeBuilder &) = delete;

        ~ExceptionTableRangeBuilder();

        void close();
        ExceptionTableRangeSuspension suspend();

    private:
        friend class ExceptionTableRangeSuspension;

        void close_segment();
        void suspend_segment();
        void resume_segment();

        CodeObjectBuilder *builder;
        JumpTarget &handler_target;
        uint32_t start_pc;
        bool closed = false;
        bool suspended = false;
    };

    class ExceptionTableRangeSuspension
    {
    public:
        explicit ExceptionTableRangeSuspension(
            ExceptionTableRangeBuilder &range);

        ExceptionTableRangeSuspension(
            ExceptionTableRangeSuspension &&other) noexcept;
        ExceptionTableRangeSuspension &
        operator=(ExceptionTableRangeSuspension &&other) noexcept;

        ExceptionTableRangeSuspension(const ExceptionTableRangeSuspension &) =
            delete;
        ExceptionTableRangeSuspension &
        operator=(const ExceptionTableRangeSuspension &) = delete;

        ~ExceptionTableRangeSuspension();

    private:
        ExceptionTableRangeBuilder *range;
    };

    class CodeObjectBuilder
    {
    public:
        struct RegisterReservation
        {
            uint32_t allocated_reg = 0;
            uint32_t allocated_n_regs = 0;
            uint32_t semantic_reg = 0;
        };

        class TemporaryReg
        {
        public:
            TemporaryReg(
                CodeObjectBuilder &_builder, uint32_t _n_regs = 1,
                RegisterAlignment _alignment = RegisterAlignment::None);

            TemporaryReg(const TemporaryReg &) = delete;
            TemporaryReg &operator=(const TemporaryReg &) = delete;

            TemporaryReg(TemporaryReg &&other) noexcept;
            TemporaryReg &operator=(TemporaryReg &&other) = delete;

            ~TemporaryReg();

            operator uint32_t() const { return reservation.semantic_reg; }

        private:
            CodeObjectBuilder *builder;
            uint32_t n_regs;
            RegisterReservation reservation;
        };

        CodeObjectBuilder(const CompilationUnit *compilation_unit,
                          TValue<ModuleObject> defining_module,
                          Scope *local_scope, TValue<String> name);
        CodeObjectBuilder(VirtualMachine *vm,
                          const CompilationUnit *compilation_unit,
                          TValue<ModuleObject> defining_module,
                          Scope *local_scope, TValue<String> name);

        CodeObjectBuilder(const CodeObjectBuilder &) = delete;
        CodeObjectBuilder &operator=(const CodeObjectBuilder &) = delete;

        CodeObjectBuilder(CodeObjectBuilder &&other) noexcept
            : code_obj(other.code_obj), finalized(other.finalized),
              temporary_reg(other.temporary_reg),
              max_temporary_reg(other.max_temporary_reg),
              constant_indices_by_raw_value(
                  std::move(other.constant_indices_by_raw_value))
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

        Scope *local_scope() const
        {
            assert(code_obj != nullptr);
            return code_obj->local_scope.extract();
        }

        TValue<ModuleObject> defining_module() const
        {
            assert(code_obj != nullptr);
            return code_obj->get_defining_module();
        }

        TValue<String> name() const
        {
            assert(code_obj != nullptr);
            return code_obj->name.value();
        }

        void set_name(TValue<String> name)
        {
            assert_not_finalized();
            code_obj->name = name;
        }

        void set_docstring(Optional<TValue<String>> docstring)
        {
            assert_not_finalized();
            code_obj->docstring = docstring;
        }

        uint32_t &n_parameters()
        {
            assert_not_finalized();
            return code_obj->function_signature.n_parameters;
        }

        uint32_t n_parameters() const
        {
            assert(code_obj != nullptr);
            return code_obj->function_signature.n_parameters;
        }

        uint32_t &n_positional_parameters()
        {
            assert_not_finalized();
            return code_obj->function_signature.n_positional_parameters;
        }

        uint32_t n_positional_parameters() const
        {
            assert(code_obj != nullptr);
            return code_obj->function_signature.n_positional_parameters;
        }

        FunctionParameterFlags &parameter_flags()
        {
            assert_not_finalized();
            return code_obj->function_signature.parameter_flags;
        }

        FunctionSignature &function_signature()
        {
            assert_not_finalized();
            return code_obj->function_signature;
        }

        FunctionKeywordRemap &function_keyword_remap()
        {
            assert_not_finalized();
            return code_obj->function_keyword_remap;
        }

        uint32_t get_padded_n_parameters() const
        {
            assert(code_obj != nullptr);
            return code_obj->get_padded_n_parameters();
        }

        int8_t encode_reg(uint32_t reg) const
        {
            assert(code_obj != nullptr);
            return code_obj->encode_reg(reg);
        }

        bool is_call_frame_aligned_register(uint32_t first_arg_reg) const;
        void assert_call_args_are_topmost(uint32_t first_arg_reg,
                                          uint32_t n_call_arg_regs) const;

        size_t size() const
        {
            assert(code_obj != nullptr);
            return code_obj->size();
        }

        Expected<uint32_t> emit_clear_local(uint32_t source_offset,
                                            uint32_t reg);
        Expected<uint32_t> emit_ldar(uint32_t source_offset, uint32_t reg);
        Expected<uint32_t> emit_load_local_checked(uint32_t source_offset,
                                                   uint32_t reg);
        Expected<uint32_t> emit_lda_global(uint32_t source_offset,
                                           uint8_t name_idx);
        Expected<uint32_t> emit_star(uint32_t source_offset, uint32_t reg);
        Expected<uint32_t> emit_mov(uint32_t source_offset, uint32_t dst_reg,
                                    uint32_t src_reg);
        Expected<uint32_t> emit_sta_global(uint32_t source_offset,
                                           uint8_t name_idx);
        Expected<uint32_t> emit_del_local(uint32_t source_offset, uint32_t reg);
        Expected<uint32_t> emit_del_global(uint32_t source_offset,
                                           uint8_t name_idx);
        Expected<uint32_t> emit_lda_none(uint32_t source_offset);
        Expected<uint32_t> emit_lda_true(uint32_t source_offset);
        Expected<uint32_t> emit_lda_false(uint32_t source_offset);
        Expected<uint32_t> emit_lda_smi(uint32_t source_offset, int8_t smi);
        Expected<uint32_t> emit_lda_constant(uint32_t source_offset,
                                             uint8_t constant_idx);
        Expected<uint32_t> emit_return(uint32_t source_offset);
        Expected<uint32_t>
        emit_return_or_raise_exception(uint32_t source_offset);
        Expected<uint32_t> emit_return_to_native(uint32_t source_offset);
        Expected<uint32_t>
        emit_return_exception_marker_to_native(uint32_t source_offset);
        Expected<uint32_t> emit_lda_active_exception(uint32_t source_offset);
        Expected<uint32_t>
        emit_active_exception_is_instance(uint32_t source_offset);
        Expected<uint32_t>
        emit_drain_active_exception_into(uint32_t source_offset, uint32_t reg);
        Expected<uint32_t> emit_clear_active_exception(uint32_t source_offset);
        Expected<uint32_t>
        emit_reraise_active_exception(uint32_t source_offset);
        Expected<uint32_t> emit_build_class(uint32_t source_offset);
        Expected<uint32_t>
        emit_check_init_returned_none(uint32_t source_offset);
        Expected<uint32_t> emit_raise_assertion_error(uint32_t source_offset);
        Expected<uint32_t>
        emit_raise_assertion_error_with_message(uint32_t source_offset);
        Expected<uint32_t> emit_raise_unwind(uint32_t source_offset);
        Expected<uint32_t>
        emit_raise_unwind_with_context(uint32_t source_offset,
                                       uint32_t context_reg);
        Expected<uint32_t> emit_raise_bare(uint32_t source_offset);
        Expected<uint32_t> emit_write_stdout(uint32_t source_offset);
        Expected<uint32_t>
        emit_create_instance_known_class(uint32_t source_offset,
                                         uint8_t class_idx);
        Expected<uint32_t>
        emit_is_instance_of_known_class(uint32_t source_offset,
                                        uint8_t class_idx);
        Expected<uint32_t> emit_create_function(uint32_t source_offset,
                                                uint8_t code_idx);
        Expected<uint32_t> emit_create_function_with_defaults(
            uint32_t source_offset, uint8_t code_idx, uint32_t defaults_reg);
        Expected<uint32_t> emit_create_tuple(uint32_t source_offset,
                                             uint32_t first_reg,
                                             uint8_t n_regs);
        Expected<uint32_t> emit_create_list(uint32_t source_offset,
                                            uint32_t first_reg, uint8_t n_regs);
        Expected<uint32_t> emit_create_dict(uint32_t source_offset,
                                            uint32_t first_reg,
                                            uint8_t n_entries);
        Expected<uint32_t> emit_create_class(uint32_t source_offset,
                                             uint8_t body_constant_idx,
                                             uint32_t first_arg_reg);
        Expected<uint32_t> emit_load_attr(uint32_t source_offset,
                                          uint32_t receiver_reg,
                                          uint8_t name_idx);
        Expected<uint32_t> emit_store_attr(uint32_t source_offset,
                                           uint32_t receiver_reg,
                                           uint8_t name_idx);
        Expected<uint32_t> emit_del_attr(uint32_t source_offset,
                                         uint32_t receiver_reg,
                                         uint8_t name_idx);
        Expected<uint32_t>
        emit_call_method_attr_positional(uint32_t source_offset,
                                         uint32_t first_arg_reg,
                                         uint8_t name_idx, uint8_t argc);
        Expected<uint32_t> emit_call_method_attr_keyword(
            uint32_t source_offset, uint32_t first_arg_reg, uint8_t name_idx,
            uint8_t n_pos_args, uint32_t first_kw_value_reg, uint8_t n_kw_args,
            uint8_t keyword_names_idx);
        Expected<uint32_t>
        emit_call_special_method(uint32_t source_offset, uint32_t first_arg_reg,
                                 uint8_t name_idx, uint8_t argc,
                                 uint8_t missing_exception_type_idx,
                                 uint8_t missing_exception_message_idx);
        Expected<uint32_t> emit_load_subscript(uint32_t source_offset,
                                               uint32_t receiver_reg);
        Expected<uint32_t> emit_store_subscript(uint32_t source_offset,
                                                uint32_t receiver_reg,
                                                uint32_t key_reg);
        Expected<uint32_t> emit_del_subscript(uint32_t source_offset,
                                              uint32_t receiver_reg,
                                              uint32_t key_reg);
        Expected<uint32_t> emit_jump(uint32_t source_offset,
                                     JumpTarget &target);
        Expected<uint32_t> emit_jump_if_false(uint32_t source_offset,
                                              JumpTarget &target);
        Expected<uint32_t> emit_jump_if_true(uint32_t source_offset,
                                             JumpTarget &target);
        Expected<uint32_t> emit_for_iter(uint32_t source_offset,
                                         uint32_t iterator_reg,
                                         JumpTarget &target);
        Expected<uint32_t> emit_for_prep_range(uint32_t source_offset,
                                               Bytecode op, uint32_t range_regs,
                                               JumpTarget &target);
        Expected<uint32_t> emit_for_iter_range(uint32_t source_offset,
                                               Bytecode op, uint32_t range_regs,
                                               JumpTarget &target);
        Expected<uint32_t> emit_binary_op(uint32_t source_offset, Bytecode op,
                                          uint32_t lhs_reg);
        Expected<uint32_t> emit_binary_smi_op(uint32_t source_offset,
                                              Bytecode op, int8_t rhs);
        Expected<uint32_t> emit_compare_op(uint32_t source_offset, Bytecode op,
                                           uint32_t lhs_reg);
        Expected<uint32_t> emit_unary_op(uint32_t source_offset, Bytecode op);
        Expected<uint32_t> emit_call_code_object(uint32_t source_offset,
                                                 uint8_t code_object_idx,
                                                 uint32_t first_arg_reg,
                                                 uint8_t argc);
        Expected<uint32_t> emit_import_name(uint32_t source_offset,
                                            uint8_t name_idx, uint8_t level);
        Expected<uint32_t> emit_import_from(uint32_t source_offset,
                                            uint8_t name_idx);
        Expected<uint32_t> emit_call_intrinsic(uint32_t source_offset,
                                               Bytecode op, uint8_t target_idx);
        Expected<uint32_t> emit_call_extension(uint32_t source_offset,
                                               Bytecode op, uint8_t target_idx);
        Expected<uint32_t>
        emit_call_runtime_intrinsic0(uint32_t source_offset,
                                     RuntimeIntrinsic0 intrinsic);
        Expected<uint32_t> emit_call_positional(uint32_t source_offset,
                                                uint32_t callable_reg,
                                                uint32_t first_arg_reg,
                                                uint8_t argc);
        Expected<uint32_t>
        emit_call_keyword(uint32_t source_offset, uint32_t callable_reg,
                          uint32_t first_arg_reg, uint8_t n_pos_args,
                          uint32_t first_kw_value_reg, uint8_t n_kw_args,
                          uint8_t keyword_names_idx);
        uint32_t add_exception_table_entry(JumpTarget &start, JumpTarget &end,
                                           JumpTarget &handler);
        uint32_t add_exception_table_entry(uint32_t start_pc, uint32_t end_pc,
                                           JumpTarget &handler);

        Expected<uint8_t> allocate_constant(Value val);
        Expected<uint8_t> allocate_constant(TValue<String> val)
        {
            return allocate_constant(val.raw_value());
        }
        Expected<uint8_t>
        add_native_function_target(NativeFunctionTarget target);

        Expected<CodeObject *> finalize();

    private:
        friend class JumpTarget;

        Expected<uint8_t> allocate_attribute_read_cache();
        Expected<uint8_t> allocate_attribute_mutation_cache();
        Expected<uint8_t> allocate_module_global_read_cache();
        Expected<uint8_t> allocate_module_global_mutation_cache();
        Expected<uint8_t> allocate_function_call_cache();
        Expected<uint8_t> allocate_keyword_call_cache();
        uint32_t emplace_back(uint32_t source_offset, uint8_t c);
        Expected<uint32_t> emit_opcode(uint32_t source_offset, Bytecode c);
        Expected<uint32_t> emit_opcode_smi(uint32_t source_offset, Bytecode c,
                                           int8_t smi);
        Expected<uint32_t> emit_opcode_constant_idx(uint32_t source_offset,
                                                    Bytecode c,
                                                    uint8_t constant_idx);
        Expected<uint32_t>
        emit_opcode_constant_idx_cache_idx(uint32_t source_offset, Bytecode c,
                                           uint8_t constant_idx,
                                           uint8_t cache_idx);
        Expected<uint32_t> emit_opcode_constant_idx_reg(uint32_t source_offset,
                                                        Bytecode c,
                                                        uint8_t constant_idx,
                                                        uint32_t reg);
        Expected<uint32_t>
        emit_opcode_constant_idx_reg_argc(uint32_t source_offset, Bytecode c,
                                          uint8_t constant_idx, uint32_t reg,
                                          uint8_t argc);
        Expected<uint32_t> emit_opcode_reg(uint32_t source_offset, Bytecode c,
                                           uint32_t reg);
        Expected<uint32_t> emit_opcode_reg_range(uint32_t source_offset,
                                                 Bytecode c, uint32_t reg,
                                                 uint8_t n_regs);
        Expected<uint32_t> emit_opcode_reg_constant_idx(uint32_t source_offset,
                                                        Bytecode c,
                                                        uint32_t reg,
                                                        uint8_t constant_idx);
        Expected<uint32_t> emit_opcode_native_target_idx(uint32_t source_offset,
                                                         Bytecode c,
                                                         uint8_t target_idx);
        Expected<uint32_t> emit_opcode_reg_constant_idx_cache_idx(
            uint32_t source_offset, Bytecode c, uint32_t reg,
            uint8_t constant_idx, uint8_t cache_idx);
        Expected<uint32_t> emit_opcode_reg_constant_idx_cache_idx_argc(
            uint32_t source_offset, Bytecode c, uint32_t reg,
            uint8_t constant_idx, uint8_t read_cache_idx,
            uint8_t call_cache_idx, uint8_t argc);
        Expected<uint32_t> emit_opcode_reg_reg(uint32_t source_offset,
                                               Bytecode c, uint32_t first_reg,
                                               uint32_t second_reg);
        Expected<uint32_t> emit_opcode_reg_jump(uint32_t source_offset,
                                                Bytecode c, uint32_t reg,
                                                JumpTarget &target);
        Expected<uint32_t> emit_jump(uint32_t source_offset, Bytecode c,
                                     JumpTarget &target);
        void set_int16(uint32_t pos, int16_t v);
        void set_exception_table_start_pc(uint32_t entry_idx, uint32_t pc);
        void set_exception_table_end_pc(uint32_t entry_idx, uint32_t pc);
        void set_exception_table_handler_pc(uint32_t entry_idx, uint32_t pc);
        void set_encoded_reg(uint32_t pos, uint32_t reg);
        void assert_not_finalized() const;
        uint32_t first_temporary_reg() const;
        void sync_temporary_reg_base();
        RegisterReservation reserve_registers(uint32_t n_regs,
                                              RegisterAlignment alignment);
        void release_registers(RegisterReservation reservation);

        CodeObject *code_obj;
        bool finalized = false;
        uint32_t temporary_reg = FrameHeaderSize;
        uint32_t max_temporary_reg = FrameHeaderSize;
        std::unordered_map<uint64_t, uint32_t> constant_indices_by_raw_value;
    };

}  // namespace cl

#endif  // CL_CODE_OBJECT_BUILDER_H
