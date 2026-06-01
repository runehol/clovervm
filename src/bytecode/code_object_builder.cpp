#include "bytecode/code_object_builder.h"

#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>

namespace cl
{
    namespace
    {
        static constexpr uint32_t kMaxU8Operand = 255;

        bool pending_exception_is_propagating()
        {
            ThreadState *thread = ThreadState::get_active_or_null();
            return thread != nullptr && thread->has_pending_exception();
        }

        Expected<uint8_t> check_u8_operand_index(uint32_t idx,
                                                 const wchar_t *table_name)
        {
            if(idx > kMaxU8Operand)
            {
                std::wstring message = table_name;
                message += L" index out of range";
                return Expected<uint8_t>::raise_exception(L"SystemError",
                                                          message.c_str());
            }
            return Expected<uint8_t>::ok(uint8_t(idx));
        }
    }  // namespace

    JumpTarget::~JumpTarget()
    {
        if(std::uncaught_exceptions() > 0 || pending_exception_is_propagating())
        {
            return;
        }
        assert(target != -1);
        assert(unresolved_relocations.empty());
    }

    Expected<void> JumpTarget::add_relocation(uint32_t pos)
    {
        return add_bytecode_relative_i16_relocation(pos);
    }

    Expected<void>
    JumpTarget::add_bytecode_relative_i16_relocation(uint32_t operand_offset)
    {
        return add_relocation(JumpRelocation{
            JumpRelocationKind::BytecodeRelativeI16, operand_offset});
    }

    Expected<void>
    JumpTarget::add_exception_table_start_absolute_u32_relocation(
        uint32_t entry_idx)
    {
        return add_relocation(JumpRelocation{
            JumpRelocationKind::ExceptionTableStartAbsoluteU32, entry_idx});
    }

    Expected<void> JumpTarget::add_exception_table_end_absolute_u32_relocation(
        uint32_t entry_idx)
    {
        return add_relocation(JumpRelocation{
            JumpRelocationKind::ExceptionTableEndAbsoluteU32, entry_idx});
    }

    Expected<void>
    JumpTarget::add_exception_table_handler_absolute_u32_relocation(
        uint32_t entry_idx)
    {
        return add_relocation(JumpRelocation{
            JumpRelocationKind::ExceptionTableHandlerAbsoluteU32, entry_idx});
    }

    Expected<void> JumpTarget::add_relocation(JumpRelocation relocation)
    {
        if(target == -1)
        {
            unresolved_relocations.push_back(relocation);
            return Expected<void>::ok();
        }
        else
        {
            return resolve_relocation(relocation);
        }
    }

    Expected<void> JumpTarget::resolve_relocation(JumpRelocation relocation)
    {
        switch(relocation.kind)
        {
            case JumpRelocationKind::BytecodeRelativeI16:
                {
                    uint32_t pos = relocation.index;
                    int32_t rel_dest = target - (pos + 2);
                    if(rel_dest != int16_t(rel_dest))
                    {
                        return Expected<void>::raise_exception(
                            L"SystemError", L"Relocation out of range");
                    }
                    builder->set_int16(pos, rel_dest);
                    break;
                }
            case JumpRelocationKind::ExceptionTableStartAbsoluteU32:
                builder->set_exception_table_start_pc(relocation.index, target);
                break;
            case JumpRelocationKind::ExceptionTableEndAbsoluteU32:
                builder->set_exception_table_end_pc(relocation.index, target);
                break;
            case JumpRelocationKind::ExceptionTableHandlerAbsoluteU32:
                builder->set_exception_table_handler_pc(relocation.index,
                                                        target);
                break;
        }
        return Expected<void>::ok();
    }

    Expected<void> JumpTarget::resolve()
    {
        assert(target == -1);
        target = builder->size();
        for(JumpRelocation relocation: unresolved_relocations)
        {
            CL_TRY(resolve_relocation(relocation));
        }
        unresolved_relocations.clear();
        return Expected<void>::ok();
    }

    ExceptionTableRangeBuilder::ExceptionTableRangeBuilder(
        CodeObjectBuilder *_builder, JumpTarget &_handler_target)
        : builder(_builder), handler_target(_handler_target),
          start_pc(_builder->size())
    {
    }

    ExceptionTableRangeBuilder::~ExceptionTableRangeBuilder()
    {
        if(std::uncaught_exceptions() > 0 || pending_exception_is_propagating())
        {
            return;
        }
        assert(closed);
    }

    void ExceptionTableRangeBuilder::close()
    {
        assert(!closed);
        assert(!suspended);
        close_segment();
        closed = true;
    }

    ExceptionTableRangeSuspension ExceptionTableRangeBuilder::suspend()
    {
        return ExceptionTableRangeSuspension(*this);
    }

    void ExceptionTableRangeBuilder::close_segment()
    {
        // Ranges are appended at close time. With depth-first AST codegen,
        // inner protected ranges close before outer ranges, which puts
        // overlapping entries in exception-table priority order.
        builder->add_exception_table_entry(start_pc, builder->size(),
                                           handler_target);
    }

    void ExceptionTableRangeBuilder::suspend_segment()
    {
        assert(!closed);
        assert(!suspended);
        close_segment();
        suspended = true;
    }

    void ExceptionTableRangeBuilder::resume_segment()
    {
        assert(!closed);
        assert(suspended);
        start_pc = builder->size();
        suspended = false;
    }

    ExceptionTableRangeSuspension::ExceptionTableRangeSuspension(
        ExceptionTableRangeBuilder &_range)
        : range(&_range)
    {
        range->suspend_segment();
    }

    ExceptionTableRangeSuspension::ExceptionTableRangeSuspension(
        ExceptionTableRangeSuspension &&other) noexcept
        : range(other.range)
    {
        other.range = nullptr;
    }

    ExceptionTableRangeSuspension &ExceptionTableRangeSuspension::operator=(
        ExceptionTableRangeSuspension &&other) noexcept
    {
        if(this == &other)
        {
            return *this;
        }
        if(range != nullptr)
        {
            range->resume_segment();
        }
        range = other.range;
        other.range = nullptr;
        return *this;
    }

    ExceptionTableRangeSuspension::~ExceptionTableRangeSuspension()
    {
        if(range != nullptr)
        {
            range->resume_segment();
        }
    }

    CodeObjectBuilder::TemporaryReg::TemporaryReg(CodeObjectBuilder &_builder,
                                                  uint32_t _n_regs,
                                                  RegisterAlignment _alignment)
        : builder(&_builder), n_regs(_n_regs)
    {
        reservation = builder->reserve_registers(n_regs, _alignment);
    }

    CodeObjectBuilder::TemporaryReg::TemporaryReg(TemporaryReg &&other) noexcept
        : builder(other.builder), n_regs(other.n_regs),
          reservation(other.reservation)
    {
        other.builder = nullptr;
        other.n_regs = 0;
        other.reservation = {};
    }

    CodeObjectBuilder::TemporaryReg::~TemporaryReg()
    {
        if(builder == nullptr)
        {
            return;
        }
        builder->release_registers(reservation);
    }

    CodeObjectBuilder::CodeObjectBuilder(
        const CompilationUnit *compilation_unit,
        TValue<ModuleObject> defining_module, Scope *local_scope,
        TValue<String> name)
        : code_obj(make_object_raw<CodeObject>(
              compilation_unit, defining_module, local_scope, name))
    {
    }

    CodeObjectBuilder::CodeObjectBuilder(
        VirtualMachine *vm, const CompilationUnit *compilation_unit,
        TValue<ModuleObject> defining_module, Scope *local_scope,
        TValue<String> name)
        : code_obj(vm->make_immortal_object_raw<CodeObject>(
              compilation_unit, defining_module, local_scope, name))
    {
    }

    uint32_t CodeObjectBuilder::first_temporary_reg() const
    {
        assert(code_obj != nullptr);
        if(code_obj->local_scope == nullptr)
        {
            return FrameHeaderSize;
        }
        return get_local_scope_ptr()->size();
    }

    bool CodeObjectBuilder::is_call_frame_aligned_register(
        uint32_t first_arg_reg) const
    {
        int32_t first_arg_encoded_reg = encode_reg(first_arg_reg);
        int32_t new_fp_reg = first_arg_encoded_reg + 1 - FrameHeaderSizeAboveFp;
        return (new_fp_reg & 1) == 0;
    }

    void CodeObjectBuilder::assert_call_args_are_topmost(
        uint32_t first_arg_reg, uint32_t n_call_arg_regs) const
    {
        assert(n_call_arg_regs > 0);
        assert(is_call_frame_aligned_register(first_arg_reg));
        assert(first_arg_reg + n_call_arg_regs == temporary_reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_clear_local(uint32_t source_offset, uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::ClearLocal, reg);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_ldar(uint32_t source_offset,
                                                    uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::Ldar, reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_load_local_checked(uint32_t source_offset,
                                               uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::LoadLocalChecked, reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_lda_global(uint32_t source_offset, uint8_t name_idx)
    {
        uint8_t cache_idx = CL_TRY(allocate_module_global_read_cache());
        return emit_opcode_constant_idx_cache_idx(
            source_offset, Bytecode::LdaGlobal, name_idx, cache_idx);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_star(uint32_t source_offset,
                                                    uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::Star, reg);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_mov(uint32_t source_offset,
                                                   uint32_t dst_reg,
                                                   uint32_t src_reg)
    {
        return emit_opcode_reg_reg(source_offset, Bytecode::Mov, dst_reg,
                                   src_reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_sta_global(uint32_t source_offset, uint8_t name_idx)
    {
        uint8_t cache_idx = CL_TRY(allocate_module_global_mutation_cache());
        return emit_opcode_constant_idx_cache_idx(
            source_offset, Bytecode::StaGlobal, name_idx, cache_idx);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_del_local(uint32_t source_offset,
                                                         uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::DelLocal, reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_del_global(uint32_t source_offset, uint8_t name_idx)
    {
        return emit_opcode_constant_idx(source_offset, Bytecode::DelGlobal,
                                        name_idx);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_lda_none(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::LdaNone);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_lda_true(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::LdaTrue);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_lda_false(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::LdaFalse);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_lda_smi(uint32_t source_offset,
                                                       int8_t smi)
    {
        return emit_opcode_smi(source_offset, Bytecode::LdaSmi, smi);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_lda_constant(uint32_t source_offset,
                                         uint8_t constant_idx)
    {
        return emit_opcode_constant_idx(source_offset, Bytecode::LdaConstant,
                                        constant_idx);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_return(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::Return);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_return_or_raise_exception(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::ReturnOrRaiseException);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_return_to_native(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::ReturnToNative);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_return_exception_marker_to_native(
        uint32_t source_offset)
    {
        return emit_opcode(source_offset,
                           Bytecode::ReturnExceptionMarkerToNative);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_lda_active_exception(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::LdaActiveException);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_active_exception_is_instance(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::ActiveExceptionIsInstance);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_drain_active_exception_into(uint32_t source_offset,
                                                        uint32_t reg)
    {
        return emit_opcode_reg(source_offset,
                               Bytecode::DrainActiveExceptionInto, reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_clear_active_exception(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::ClearActiveException);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_reraise_active_exception(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::ReraiseActiveException);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_build_class(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::BuildClass);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_check_init_returned_none(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::CheckInitReturnedNone);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_raise_assertion_error(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::RaiseAssertionError);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_raise_assertion_error_with_message(
        uint32_t source_offset)
    {
        return emit_opcode(source_offset,
                           Bytecode::RaiseAssertionErrorWithMessage);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_raise_unwind(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::RaiseUnwind);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_raise_unwind_with_context(uint32_t source_offset,
                                                      uint32_t context_reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::RaiseUnwindWithContext,
                               context_reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_raise_bare(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::RaiseBare);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_write_stdout(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::WriteStdout);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_create_instance_known_class(uint32_t source_offset,
                                                        uint8_t class_idx)
    {
        return emit_opcode_constant_idx(
            source_offset, Bytecode::CreateInstanceKnownClass, class_idx);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_is_instance_of_known_class(uint32_t source_offset,
                                                       uint8_t class_idx)
    {
        return emit_opcode_constant_idx(
            source_offset, Bytecode::IsInstanceOfKnownClass, class_idx);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_create_function(uint32_t source_offset,
                                            uint8_t code_idx)
    {
        return emit_opcode_constant_idx(source_offset, Bytecode::CreateFunction,
                                        code_idx);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_create_function_with_defaults(
        uint32_t source_offset, uint8_t code_idx, uint32_t defaults_reg)
    {
        return emit_opcode_constant_idx_reg(
            source_offset, Bytecode::CreateFunctionWithDefaults, code_idx,
            defaults_reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_create_tuple(uint32_t source_offset,
                                         uint32_t first_reg, uint8_t n_regs)
    {
        return emit_opcode_reg_range(source_offset, Bytecode::CreateTuple,
                                     first_reg, n_regs);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_create_list(uint32_t source_offset,
                                        uint32_t first_reg, uint8_t n_regs)
    {
        return emit_opcode_reg_range(source_offset, Bytecode::CreateList,
                                     first_reg, n_regs);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_create_dict(uint32_t source_offset,
                                        uint32_t first_reg, uint8_t n_entries)
    {
        return emit_opcode_reg_range(source_offset, Bytecode::CreateDict,
                                     first_reg, n_entries);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_create_class(uint32_t source_offset,
                                         uint8_t body_constant_idx,
                                         uint32_t first_arg_reg)
    {
        assert_call_args_are_topmost(first_arg_reg, ClassBodyParameterCount);
        return emit_opcode_constant_idx_reg(source_offset,
                                            Bytecode::CreateClass,
                                            body_constant_idx, first_arg_reg);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_load_attr(uint32_t source_offset,
                                                         uint32_t receiver_reg,
                                                         uint8_t name_idx)
    {
        uint8_t cache_idx = CL_TRY(allocate_attribute_read_cache());
        return emit_opcode_reg_constant_idx_cache_idx(
            source_offset, Bytecode::LoadAttr, receiver_reg, name_idx,
            cache_idx);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_store_attr(uint32_t source_offset,
                                       uint32_t receiver_reg, uint8_t name_idx)
    {
        uint8_t cache_idx = CL_TRY(allocate_attribute_mutation_cache());
        return emit_opcode_reg_constant_idx_cache_idx(
            source_offset, Bytecode::StoreAttr, receiver_reg, name_idx,
            cache_idx);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_del_attr(uint32_t source_offset,
                                                        uint32_t receiver_reg,
                                                        uint8_t name_idx)
    {
        uint8_t cache_idx = CL_TRY(allocate_attribute_mutation_cache());
        return emit_opcode_reg_constant_idx_cache_idx(
            source_offset, Bytecode::DelAttr, receiver_reg, name_idx,
            cache_idx);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_call_method_attr_positional(
        uint32_t source_offset, uint32_t first_arg_reg, uint8_t name_idx,
        uint8_t argc)
    {
        assert_call_args_are_topmost(first_arg_reg, uint32_t(argc) + 1);
        uint8_t read_cache_idx = CL_TRY(allocate_attribute_read_cache());
        uint8_t call_cache_idx = CL_TRY(allocate_function_call_cache());
        return emit_opcode_reg_constant_idx_cache_idx_argc(
            source_offset, Bytecode::CallMethodAttrPositional, first_arg_reg,
            name_idx, read_cache_idx, call_cache_idx, argc);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_call_method_attr_keyword(
        uint32_t source_offset, uint32_t first_arg_reg, uint8_t name_idx,
        uint8_t n_pos_args, uint32_t first_kw_value_reg, uint8_t n_kw_args,
        uint8_t keyword_names_idx)
    {
        assert_call_args_are_topmost(first_arg_reg, uint32_t(n_pos_args) + 1);
        uint8_t read_cache_idx = CL_TRY(allocate_attribute_read_cache());
        uint8_t call_cache_idx = CL_TRY(allocate_keyword_call_cache());
        uint32_t result = emplace_back(
            source_offset, uint8_t(Bytecode::CallMethodAttrKeyword));
        emplace_back(source_offset, encode_reg(first_arg_reg));
        emplace_back(source_offset, name_idx);
        emplace_back(source_offset, read_cache_idx);
        emplace_back(source_offset, call_cache_idx);
        emplace_back(source_offset, n_pos_args);
        emplace_back(source_offset, encode_reg(first_kw_value_reg));
        emplace_back(source_offset, n_kw_args);
        emplace_back(source_offset, keyword_names_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_call_special_method(
        uint32_t source_offset, uint32_t first_arg_reg, uint8_t name_idx,
        uint8_t argc, uint8_t missing_exception_type_idx,
        uint8_t missing_exception_message_idx)
    {
        assert_call_args_are_topmost(first_arg_reg, uint32_t(argc) + 1);
        uint8_t read_cache_idx = CL_TRY(allocate_attribute_read_cache());
        uint8_t call_cache_idx = CL_TRY(allocate_function_call_cache());
        uint32_t result = CL_TRY(emit_opcode_reg_constant_idx_cache_idx_argc(
            source_offset, Bytecode::CallSpecialMethod, first_arg_reg, name_idx,
            read_cache_idx, call_cache_idx, argc));
        emplace_back(source_offset, missing_exception_type_idx);
        emplace_back(source_offset, missing_exception_message_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_load_subscript(uint32_t source_offset,
                                           uint32_t receiver_reg)
    {
        uint8_t cache_idx = CL_TRY(allocate_get_item_cache());
        uint32_t result =
            emplace_back(source_offset, uint8_t(Bytecode::LoadSubscript));
        emplace_back(source_offset, encode_reg(receiver_reg));
        emplace_back(source_offset, cache_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_store_subscript(
        uint32_t source_offset, uint32_t receiver_reg, uint32_t key_reg)
    {
        return emit_opcode_reg_reg(source_offset, Bytecode::StoreSubscript,
                                   receiver_reg, key_reg);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_del_subscript(
        uint32_t source_offset, uint32_t receiver_reg, uint32_t key_reg)
    {
        return emit_opcode_reg_reg(source_offset, Bytecode::DelSubscript,
                                   receiver_reg, key_reg);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_jump(uint32_t source_offset,
                                                    JumpTarget &target)
    {
        return emit_jump(source_offset, Bytecode::Jump, target);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_jump_if_false(uint32_t source_offset,
                                          JumpTarget &target)
    {
        return emit_jump(source_offset, Bytecode::JumpIfFalse, target);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_jump_if_true(uint32_t source_offset,
                                         JumpTarget &target)
    {
        return emit_jump(source_offset, Bytecode::JumpIfTrue, target);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_for_iter(uint32_t source_offset,
                                                        uint32_t iterator_reg,
                                                        JumpTarget &target)
    {
        return emit_opcode_reg_jump(source_offset, Bytecode::ForIter,
                                    iterator_reg, target);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_for_prep_range(uint32_t source_offset, Bytecode op,
                                           uint32_t range_regs,
                                           JumpTarget &target)
    {
        assert(op == Bytecode::ForPrepRange1 || op == Bytecode::ForPrepRange2 ||
               op == Bytecode::ForPrepRange3);
        return emit_opcode_reg_jump(source_offset, op, range_regs, target);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_for_iter_range(uint32_t source_offset, Bytecode op,
                                           uint32_t range_regs,
                                           JumpTarget &target)
    {
        assert(op == Bytecode::ForIterRange1 ||
               op == Bytecode::ForIterRangeStep);
        return emit_opcode_reg_jump(source_offset, op, range_regs, target);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_binary_op(uint32_t source_offset,
                                                         Bytecode op,
                                                         uint32_t lhs_reg)
    {
        return emit_opcode_reg(source_offset, op, lhs_reg);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_binary_smi_op(uint32_t source_offset, Bytecode op,
                                          int8_t rhs)
    {
        return emit_opcode_smi(source_offset, op, rhs);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_compare_op(uint32_t source_offset, Bytecode op,
                                       uint32_t lhs_reg)
    {
        return emit_opcode_reg(source_offset, op, lhs_reg);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_unary_op(uint32_t source_offset,
                                                        Bytecode op)
    {
        return emit_opcode(source_offset, op);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_call_code_object(
        uint32_t source_offset, uint8_t code_object_idx, uint32_t first_arg_reg,
        uint8_t argc)
    {
        assert_call_args_are_topmost(first_arg_reg,
                                     std::max<uint32_t>(argc, 1));
        return emit_opcode_constant_idx_reg_argc(
            source_offset, Bytecode::CallCodeObject, code_object_idx,
            first_arg_reg, argc);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_import_name(uint32_t source_offset,
                                        uint8_t name_idx, uint8_t level)
    {
        uint32_t result = CL_TRY(emit_opcode_constant_idx(
            source_offset, Bytecode::ImportName, name_idx));
        emplace_back(source_offset, level);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_import_from(uint32_t source_offset,
                                        uint8_t name_idx)
    {
        return emit_opcode_constant_idx(source_offset, Bytecode::ImportFrom,
                                        name_idx);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_call_intrinsic(uint32_t source_offset, Bytecode op,
                                           uint8_t target_idx)
    {
        assert(
            op == Bytecode::CallIntrinsic0 || op == Bytecode::CallIntrinsic1 ||
            op == Bytecode::CallIntrinsic2 || op == Bytecode::CallIntrinsic3 ||
            op == Bytecode::CallIntrinsic4 || op == Bytecode::CallIntrinsic5 ||
            op == Bytecode::CallIntrinsic6 || op == Bytecode::CallIntrinsic7);
        return emit_opcode_native_target_idx(source_offset, op, target_idx);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_call_extension(uint32_t source_offset, Bytecode op,
                                           uint8_t target_idx)
    {
        assert(
            op == Bytecode::CallExtension0 || op == Bytecode::CallExtension1 ||
            op == Bytecode::CallExtension2 || op == Bytecode::CallExtension3 ||
            op == Bytecode::CallExtension4 || op == Bytecode::CallExtension5 ||
            op == Bytecode::CallExtension6 || op == Bytecode::CallExtension7);
        return emit_opcode_native_target_idx(source_offset, op, target_idx);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_call_runtime_intrinsic0(uint32_t source_offset,
                                                    RuntimeIntrinsic0 intrinsic)
    {
        uint32_t result = emplace_back(
            source_offset, uint8_t(Bytecode::CallRuntimeIntrinsic0));
        emplace_back(source_offset, uint8_t(intrinsic));
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_call_positional(
        uint32_t source_offset, uint32_t callable_reg, uint32_t first_arg_reg,
        uint8_t argc)
    {
        assert_call_args_are_topmost(first_arg_reg,
                                     std::max<uint32_t>(argc, 1));
        uint32_t result =
            emplace_back(source_offset, uint8_t(Bytecode::CallPositional));
        uint8_t cache_idx = CL_TRY(allocate_function_call_cache());
        emplace_back(source_offset, encode_reg(callable_reg));
        emplace_back(source_offset, encode_reg(first_arg_reg));
        emplace_back(source_offset, argc);
        emplace_back(source_offset, cache_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_call_keyword(
        uint32_t source_offset, uint32_t callable_reg, uint32_t first_arg_reg,
        uint8_t n_pos_args, uint32_t first_kw_value_reg, uint8_t n_kw_args,
        uint8_t keyword_names_idx)
    {
        assert_call_args_are_topmost(first_arg_reg,
                                     std::max<uint32_t>(n_pos_args, 1));
        uint32_t result =
            emplace_back(source_offset, uint8_t(Bytecode::CallKeyword));
        uint8_t cache_idx = CL_TRY(allocate_keyword_call_cache());
        emplace_back(source_offset, encode_reg(callable_reg));
        emplace_back(source_offset, encode_reg(first_arg_reg));
        emplace_back(source_offset, n_pos_args);
        emplace_back(source_offset, encode_reg(first_kw_value_reg));
        emplace_back(source_offset, n_kw_args);
        emplace_back(source_offset, keyword_names_idx);
        emplace_back(source_offset, cache_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_constant(Value val)
    {
        uint64_t raw_value = uint64_t(val.as.integer);
        auto existing = constant_indices_by_raw_value.find(raw_value);
        if(existing != constant_indices_by_raw_value.end())
        {
            return Expected<uint8_t>::ok(uint8_t(existing->second));
        }

        uint32_t idx = code_obj->constant_table.size();
        uint8_t encoded_idx =
            CL_TRY(check_u8_operand_index(idx, L"constant table"));
        code_obj->constant_table.emplace_back(val);
        constant_indices_by_raw_value.emplace(raw_value, idx);
        return Expected<uint8_t>::ok(encoded_idx);
    }

    Expected<uint8_t>
    CodeObjectBuilder::add_native_function_target(NativeFunctionTarget target)
    {
        uint32_t idx = code_obj->native_function_targets.size();
        uint8_t encoded_idx = CL_TRY(
            check_u8_operand_index(idx, L"native function target table"));
        code_obj->native_function_targets.push_back(target);
        return Expected<uint8_t>::ok(encoded_idx);
    }

    uint32_t CodeObjectBuilder::add_exception_table_entry(JumpTarget &start,
                                                          JumpTarget &end,
                                                          JumpTarget &handler)
    {
        assert_not_finalized();
        uint32_t idx = code_obj->exception_table.size();
        code_obj->exception_table.push_back({0, 0, 0});
        start.add_exception_table_start_absolute_u32_relocation(idx).value();
        end.add_exception_table_end_absolute_u32_relocation(idx).value();
        handler.add_exception_table_handler_absolute_u32_relocation(idx)
            .value();
        return idx;
    }

    uint32_t CodeObjectBuilder::add_exception_table_entry(uint32_t start_pc,
                                                          uint32_t end_pc,
                                                          JumpTarget &handler)
    {
        assert_not_finalized();
        uint32_t idx = code_obj->exception_table.size();
        code_obj->exception_table.push_back({start_pc, end_pc, 0});
        handler.add_exception_table_handler_absolute_u32_relocation(idx)
            .value();
        return idx;
    }

    Expected<CodeObject *> CodeObjectBuilder::finalize()
    {
        assert_not_finalized();
        uint32_t local_scope_size = FrameHeaderSize;
        if(code_obj->local_scope != nullptr)
        {
            local_scope_size = get_local_scope_ptr()->size();
            uint32_t named_local_and_header_slots =
                local_scope_size - get_padded_n_parameters();
            assert(named_local_and_header_slots >= FrameHeaderSize);
            code_obj->n_locals = named_local_and_header_slots - FrameHeaderSize;
        }
        sync_temporary_reg_base();
        assert(temporary_reg == local_scope_size);
        assert(max_temporary_reg >= local_scope_size);
        code_obj->n_temporaries = max_temporary_reg - local_scope_size;
        finalized = true;
        return Expected<CodeObject *>::ok(code_obj);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_attribute_read_cache()
    {
        uint32_t idx = code_obj->attribute_read_caches.size();
        uint8_t encoded_idx =
            CL_TRY(check_u8_operand_index(idx, L"attribute read cache table"));
        code_obj->attribute_read_caches.emplace_back();
        return Expected<uint8_t>::ok(encoded_idx);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_attribute_mutation_cache()
    {
        uint32_t idx = code_obj->attribute_mutation_caches.size();
        uint8_t encoded_idx = CL_TRY(
            check_u8_operand_index(idx, L"attribute mutation cache table"));
        code_obj->attribute_mutation_caches.emplace_back();
        return Expected<uint8_t>::ok(encoded_idx);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_module_global_read_cache()
    {
        uint32_t idx = code_obj->module_global_read_caches.size();
        uint8_t encoded_idx = CL_TRY(
            check_u8_operand_index(idx, L"module global read cache table"));
        code_obj->module_global_read_caches.emplace_back();
        return Expected<uint8_t>::ok(encoded_idx);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_module_global_mutation_cache()
    {
        uint32_t idx = code_obj->module_global_mutation_caches.size();
        uint8_t encoded_idx = CL_TRY(
            check_u8_operand_index(idx, L"module global mutation cache table"));
        code_obj->module_global_mutation_caches.emplace_back();
        return Expected<uint8_t>::ok(encoded_idx);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_function_call_cache()
    {
        uint32_t idx = code_obj->function_call_caches.size();
        uint8_t encoded_idx =
            CL_TRY(check_u8_operand_index(idx, L"function call cache table"));
        code_obj->function_call_caches.emplace_back();
        return Expected<uint8_t>::ok(encoded_idx);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_get_item_cache()
    {
        uint32_t idx = code_obj->get_item_caches.size();
        uint8_t encoded_idx =
            CL_TRY(check_u8_operand_index(idx, L"get-item cache table"));
        code_obj->get_item_caches.emplace_back();
        return Expected<uint8_t>::ok(encoded_idx);
    }

    Expected<uint8_t> CodeObjectBuilder::allocate_keyword_call_cache()
    {
        uint32_t idx = code_obj->keyword_call_caches.size();
        uint8_t encoded_idx =
            CL_TRY(check_u8_operand_index(idx, L"keyword call cache table"));
        code_obj->keyword_call_caches.emplace_back();
        return Expected<uint8_t>::ok(encoded_idx);
    }

    uint32_t CodeObjectBuilder::emplace_back(uint32_t source_offset, uint8_t c)
    {
        assert_not_finalized();
        uint32_t offset = code_obj->code.size();
        code_obj->code.push_back(c);
        code_obj->source_offsets.push_back(source_offset);
        return offset;
    }

    Expected<uint32_t> CodeObjectBuilder::emit_opcode(uint32_t source_offset,
                                                      Bytecode c)
    {
        assert(c != Bytecode::Invalid);
        return Expected<uint32_t>::ok(emplace_back(source_offset, uint8_t(c)));
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_opcode_smi(uint32_t source_offset, Bytecode c,
                                       int8_t smi)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, smi);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_opcode_constant_idx(
        uint32_t source_offset, Bytecode c, uint8_t constant_idx)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_opcode_constant_idx_cache_idx(
        uint32_t source_offset, Bytecode c, uint8_t constant_idx,
        uint8_t cache_idx)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, cache_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_opcode_constant_idx_reg(
        uint32_t source_offset, Bytecode c, uint8_t constant_idx, uint32_t reg)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, encode_reg(reg));
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_opcode_constant_idx_reg_argc(
        uint32_t source_offset, Bytecode c, uint8_t constant_idx, uint32_t reg,
        uint8_t argc)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, encode_reg(reg));
        emplace_back(source_offset, argc);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_opcode_reg(uint32_t source_offset, Bytecode c,
                                       uint32_t reg)
    {
        assert(c != Bytecode::Invalid);
        int8_t encoded_reg = encode_reg(reg);
        int8_t r_offset = -encoded_reg - FrameHeaderSizeBelowFp - 1;
        if(r_offset >= 0 && r_offset < n_fastpath_ldar_star)
        {
            if(c == Bytecode::Ldar)
            {
                return Expected<uint32_t>::ok(emplace_back(
                    source_offset, uint8_t(Bytecode::Ldar0) + r_offset));
            }
            else if(c == Bytecode::Star)
            {
                return Expected<uint32_t>::ok(emplace_back(
                    source_offset, uint8_t(Bytecode::Star0) + r_offset));
            }
        }
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encoded_reg);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_opcode_reg_range(uint32_t source_offset, Bytecode c,
                                             uint32_t reg, uint8_t n_regs)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        emplace_back(source_offset, n_regs);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_opcode_reg_constant_idx(
        uint32_t source_offset, Bytecode c, uint32_t reg, uint8_t constant_idx)
    {
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        emplace_back(source_offset, constant_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_opcode_native_target_idx(
        uint32_t source_offset, Bytecode c, uint8_t target_idx)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, target_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_opcode_reg_constant_idx_cache_idx(
        uint32_t source_offset, Bytecode c, uint32_t reg, uint8_t constant_idx,
        uint8_t cache_idx)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, cache_idx);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_opcode_reg_constant_idx_cache_idx_argc(
        uint32_t source_offset, Bytecode c, uint32_t reg, uint8_t constant_idx,
        uint8_t read_cache_idx, uint8_t call_cache_idx, uint8_t argc)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, read_cache_idx);
        emplace_back(source_offset, call_cache_idx);
        emplace_back(source_offset, argc);
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_opcode_reg_reg(uint32_t source_offset, Bytecode c,
                                           uint32_t first_reg,
                                           uint32_t second_reg)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(first_reg));
        emplace_back(source_offset, encode_reg(second_reg));
        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t>
    CodeObjectBuilder::emit_opcode_reg_jump(uint32_t source_offset, Bytecode c,
                                            uint32_t reg, JumpTarget &target)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        uint32_t pos = code_obj->code.size();
        emplace_back(source_offset, 0);
        emplace_back(source_offset, 0);
        CL_TRY(target.add_relocation(pos));

        return Expected<uint32_t>::ok(result);
    }

    Expected<uint32_t> CodeObjectBuilder::emit_jump(uint32_t source_offset,
                                                    Bytecode c,
                                                    JumpTarget &target)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        uint32_t pos = code_obj->code.size();
        emplace_back(source_offset, 0);
        emplace_back(source_offset, 0);
        CL_TRY(target.add_relocation(pos));

        return Expected<uint32_t>::ok(result);
    }

    void CodeObjectBuilder::set_int16(uint32_t pos, int16_t v)
    {
        code_obj->code[pos + 0] = (v >> 0) & 0xff;
        code_obj->code[pos + 1] = (v >> 8) & 0xff;
    }

    void CodeObjectBuilder::set_exception_table_start_pc(uint32_t entry_idx,
                                                         uint32_t pc)
    {
        assert(entry_idx < code_obj->exception_table.size());
        code_obj->exception_table[entry_idx].start_pc = pc;
    }

    void CodeObjectBuilder::set_exception_table_end_pc(uint32_t entry_idx,
                                                       uint32_t pc)
    {
        assert(entry_idx < code_obj->exception_table.size());
        code_obj->exception_table[entry_idx].end_pc = pc;
    }

    void CodeObjectBuilder::set_exception_table_handler_pc(uint32_t entry_idx,
                                                           uint32_t pc)
    {
        assert(entry_idx < code_obj->exception_table.size());
        code_obj->exception_table[entry_idx].handler_pc = pc;
    }

    void CodeObjectBuilder::set_encoded_reg(uint32_t pos, uint32_t reg)
    {
        code_obj->code[pos] = encode_reg(reg);
    }

    void CodeObjectBuilder::assert_not_finalized() const
    {
        assert(code_obj != nullptr);
        assert(!finalized);
    }

    void CodeObjectBuilder::sync_temporary_reg_base()
    {
        uint32_t base = first_temporary_reg();
        if(temporary_reg < base)
        {
            temporary_reg = base;
        }
        max_temporary_reg = std::max(max_temporary_reg, temporary_reg);
    }

    CodeObjectBuilder::RegisterReservation
    CodeObjectBuilder::reserve_registers(uint32_t n_regs,
                                         RegisterAlignment alignment)
    {
        assert_not_finalized();
        assert(n_regs > 0);
        sync_temporary_reg_base();
        uint32_t allocated_reg = temporary_reg;
        uint32_t semantic_reg = temporary_reg;
        if(alignment == RegisterAlignment::CallFrame &&
           !is_call_frame_aligned_register(semantic_reg))
        {
            ++semantic_reg;
        }
        uint32_t allocated_n_regs = semantic_reg - allocated_reg + n_regs;
        temporary_reg += allocated_n_regs;
        max_temporary_reg = std::max(max_temporary_reg, temporary_reg);
        return {allocated_reg, allocated_n_regs, semantic_reg};
    }

    void CodeObjectBuilder::release_registers(RegisterReservation reservation)
    {
        temporary_reg -= reservation.allocated_n_regs;
        assert(reservation.allocated_reg == temporary_reg);
    }
}  // namespace cl
