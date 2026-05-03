#include "code_object_builder.h"

#include "thread_state.h"
#include "virtual_machine.h"

#include <algorithm>
#include <stdexcept>

namespace cl
{
    void JumpTarget::add_relocation(uint32_t pos)
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

    void JumpTarget::resolve_relocation(uint32_t pos)
    {
        int32_t rel_dest = target - (pos + 2);
        if(rel_dest != int16_t(rel_dest))
        {
            throw std::runtime_error("Relocation out of range");
        }
        builder->set_int16(pos, rel_dest);
    }

    void JumpTarget::resolve()
    {
        target = builder->size();
        for(uint32_t pos: unresolved_relocations)
        {
            resolve_relocation(pos);
        }
        unresolved_relocations.clear();
    }

    CodeObjectBuilder::TemporaryReg::TemporaryReg(CodeObjectBuilder &_builder,
                                                  uint32_t _n_regs)
        : builder(&_builder), n_regs(_n_regs),
          reg(builder->reserve_registers(n_regs))
    {
    }

    CodeObjectBuilder::TemporaryReg::TemporaryReg(TemporaryReg &&other) noexcept
        : builder(other.builder), n_regs(other.n_regs), reg(other.reg)
    {
        other.builder = nullptr;
        other.n_regs = 0;
        other.reg = 0;
    }

    CodeObjectBuilder::TemporaryReg::~TemporaryReg()
    {
        if(builder == nullptr)
        {
            return;
        }
        builder->release_registers(reg, n_regs);
    }

    CodeObjectBuilder::CodeObjectBuilder(
        const CompilationUnit *compilation_unit, Scope *module_scope,
        Scope *local_scope, Value name)
        : code_obj(make_object_raw<CodeObject>(compilation_unit, module_scope,
                                               local_scope, name))
    {
    }

    CodeObjectBuilder::CodeObjectBuilder(
        VirtualMachine *vm, const CompilationUnit *compilation_unit,
        Scope *module_scope, Scope *local_scope, Value name)
        : code_obj(vm->make_immortal_object_raw<CodeObject>(
              compilation_unit, module_scope, local_scope, name))
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

    uint32_t CodeObjectBuilder::emit_clear_local(uint32_t source_offset,
                                                 uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::ClearLocal, reg);
    }

    uint32_t CodeObjectBuilder::emit_ldar(uint32_t source_offset, uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::Ldar, reg);
    }

    uint32_t CodeObjectBuilder::emit_load_local_checked(uint32_t source_offset,
                                                        uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::LoadLocalChecked, reg);
    }

    uint32_t CodeObjectBuilder::emit_lda_global(uint32_t source_offset,
                                                uint32_t slot_idx)
    {
        return emit_opcode_uint32(source_offset, Bytecode::LdaGlobal, slot_idx);
    }

    uint32_t CodeObjectBuilder::emit_star(uint32_t source_offset, uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::Star, reg);
    }

    uint32_t CodeObjectBuilder::emit_star(uint32_t source_offset,
                                          OutgoingArgReg reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::Star, reg);
    }

    uint32_t CodeObjectBuilder::emit_sta_global(uint32_t source_offset,
                                                uint32_t slot_idx)
    {
        return emit_opcode_uint32(source_offset, Bytecode::StaGlobal, slot_idx);
    }

    uint32_t CodeObjectBuilder::emit_del_local(uint32_t source_offset,
                                               uint32_t reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::DelLocal, reg);
    }

    uint32_t CodeObjectBuilder::emit_del_global(uint32_t source_offset,
                                                uint32_t slot_idx)
    {
        return emit_opcode_uint32(source_offset, Bytecode::DelGlobal, slot_idx);
    }

    uint32_t CodeObjectBuilder::emit_lda_none(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::LdaNone);
    }

    uint32_t CodeObjectBuilder::emit_lda_true(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::LdaTrue);
    }

    uint32_t CodeObjectBuilder::emit_lda_false(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::LdaFalse);
    }

    uint32_t CodeObjectBuilder::emit_lda_smi(uint32_t source_offset, int8_t smi)
    {
        return emit_opcode_smi(source_offset, Bytecode::LdaSmi, smi);
    }

    uint32_t CodeObjectBuilder::emit_lda_constant(uint32_t source_offset,
                                                  uint8_t constant_idx)
    {
        return emit_opcode_constant_idx(source_offset, Bytecode::LdaConstant,
                                        constant_idx);
    }

    uint32_t CodeObjectBuilder::emit_return(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::Return);
    }

    uint32_t
    CodeObjectBuilder::emit_return_or_raise_exception(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::ReturnOrRaiseException);
    }

    uint32_t
    CodeObjectBuilder::emit_raise_if_unhandled_exception(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::RaiseIfUnhandledException);
    }

    uint32_t CodeObjectBuilder::emit_halt(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::Halt);
    }

    uint32_t CodeObjectBuilder::emit_get_iter(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::GetIter);
    }

    uint32_t CodeObjectBuilder::emit_build_class(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::BuildClass);
    }

    uint32_t
    CodeObjectBuilder::emit_check_init_returned_none(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::CheckInitReturnedNone);
    }

    uint32_t
    CodeObjectBuilder::emit_raise_assertion_error(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::RaiseAssertionError);
    }

    uint32_t CodeObjectBuilder::emit_raise_assertion_error_with_message(
        uint32_t source_offset)
    {
        return emit_opcode(source_offset,
                           Bytecode::RaiseAssertionErrorWithMessage);
    }

    uint32_t CodeObjectBuilder::emit_raise_unwind(uint32_t source_offset)
    {
        return emit_opcode(source_offset, Bytecode::RaiseUnwind);
    }

    uint32_t
    CodeObjectBuilder::emit_create_instance_known_class(uint32_t source_offset,
                                                        uint8_t class_idx)
    {
        return emit_opcode_constant_idx(
            source_offset, Bytecode::CreateInstanceKnownClass, class_idx);
    }

    uint32_t CodeObjectBuilder::emit_create_function(uint32_t source_offset,
                                                     uint8_t code_idx)
    {
        return emit_opcode_constant_idx(source_offset, Bytecode::CreateFunction,
                                        code_idx);
    }

    uint32_t CodeObjectBuilder::emit_create_function_with_defaults(
        uint32_t source_offset, uint8_t code_idx, uint32_t defaults_reg)
    {
        return emit_opcode_constant_idx_reg(
            source_offset, Bytecode::CreateFunctionWithDefaults, code_idx,
            defaults_reg);
    }

    uint32_t CodeObjectBuilder::emit_create_tuple(uint32_t source_offset,
                                                  uint32_t first_reg,
                                                  uint8_t n_regs)
    {
        return emit_opcode_reg_range(source_offset, Bytecode::CreateTuple,
                                     first_reg, n_regs);
    }

    uint32_t CodeObjectBuilder::emit_create_tuple(uint32_t source_offset,
                                                  OutgoingArgReg reg,
                                                  uint8_t n_regs)
    {
        return emit_opcode_reg_range(source_offset, Bytecode::CreateTuple, reg,
                                     n_regs);
    }

    uint32_t CodeObjectBuilder::emit_create_list(uint32_t source_offset,
                                                 uint32_t first_reg,
                                                 uint8_t n_regs)
    {
        return emit_opcode_reg_range(source_offset, Bytecode::CreateList,
                                     first_reg, n_regs);
    }

    uint32_t CodeObjectBuilder::emit_create_dict(uint32_t source_offset,
                                                 uint32_t first_reg,
                                                 uint8_t n_entries)
    {
        return emit_opcode_reg_range(source_offset, Bytecode::CreateDict,
                                     first_reg, n_entries);
    }

    uint32_t CodeObjectBuilder::emit_create_class(uint32_t source_offset,
                                                  uint8_t body_constant_idx,
                                                  OutgoingArgReg first_arg_reg)
    {
        return emit_opcode_constant_idx_reg(source_offset,
                                            Bytecode::CreateClass,
                                            body_constant_idx, first_arg_reg);
    }

    uint32_t CodeObjectBuilder::emit_load_attr(uint32_t source_offset,
                                               uint32_t receiver_reg,
                                               uint8_t name_idx)
    {
        uint8_t cache_idx = allocate_attribute_read_cache();
        return emit_opcode_reg_constant_idx_cache_idx(
            source_offset, Bytecode::LoadAttr, receiver_reg, name_idx,
            cache_idx);
    }

    uint32_t CodeObjectBuilder::emit_store_attr(uint32_t source_offset,
                                                uint32_t receiver_reg,
                                                uint8_t name_idx)
    {
        uint8_t cache_idx = allocate_attribute_mutation_cache();
        return emit_opcode_reg_constant_idx_cache_idx(
            source_offset, Bytecode::StoreAttr, receiver_reg, name_idx,
            cache_idx);
    }

    uint32_t CodeObjectBuilder::emit_del_attr(uint32_t source_offset,
                                              uint32_t receiver_reg,
                                              uint8_t name_idx)
    {
        uint8_t cache_idx = allocate_attribute_mutation_cache();
        return emit_opcode_reg_constant_idx_cache_idx(
            source_offset, Bytecode::DelAttr, receiver_reg, name_idx,
            cache_idx);
    }

    uint32_t
    CodeObjectBuilder::emit_call_method_attr(uint32_t source_offset,
                                             OutgoingArgReg first_arg_reg,
                                             uint8_t name_idx, uint8_t argc)
    {
        uint8_t read_cache_idx = allocate_attribute_read_cache();
        uint8_t call_cache_idx = allocate_function_call_cache();
        return emit_opcode_reg_constant_idx_cache_idx_argc(
            source_offset, Bytecode::CallMethodAttr, first_arg_reg, name_idx,
            read_cache_idx, call_cache_idx, argc);
    }

    uint32_t CodeObjectBuilder::emit_load_subscript(uint32_t source_offset,
                                                    uint32_t receiver_reg)
    {
        return emit_opcode_reg(source_offset, Bytecode::LoadSubscript,
                               receiver_reg);
    }

    uint32_t CodeObjectBuilder::emit_store_subscript(uint32_t source_offset,
                                                     uint32_t receiver_reg,
                                                     uint32_t key_reg)
    {
        return emit_opcode_reg_reg(source_offset, Bytecode::StoreSubscript,
                                   receiver_reg, key_reg);
    }

    uint32_t CodeObjectBuilder::emit_del_subscript(uint32_t source_offset,
                                                   uint32_t receiver_reg,
                                                   uint32_t key_reg)
    {
        return emit_opcode_reg_reg(source_offset, Bytecode::DelSubscript,
                                   receiver_reg, key_reg);
    }

    uint32_t CodeObjectBuilder::emit_jump(uint32_t source_offset,
                                          JumpTarget &target)
    {
        return emit_jump(source_offset, Bytecode::Jump, target);
    }

    uint32_t CodeObjectBuilder::emit_jump_if_false(uint32_t source_offset,
                                                   JumpTarget &target)
    {
        return emit_jump(source_offset, Bytecode::JumpIfFalse, target);
    }

    uint32_t CodeObjectBuilder::emit_jump_if_true(uint32_t source_offset,
                                                  JumpTarget &target)
    {
        return emit_jump(source_offset, Bytecode::JumpIfTrue, target);
    }

    uint32_t CodeObjectBuilder::emit_for_iter(uint32_t source_offset,
                                              uint32_t iterator_reg,
                                              JumpTarget &target)
    {
        return emit_opcode_reg_jump(source_offset, Bytecode::ForIter,
                                    iterator_reg, target);
    }

    uint32_t CodeObjectBuilder::emit_for_prep_range(uint32_t source_offset,
                                                    Bytecode op,
                                                    uint32_t range_regs,
                                                    JumpTarget &target)
    {
        assert(op == Bytecode::ForPrepRange1 || op == Bytecode::ForPrepRange2 ||
               op == Bytecode::ForPrepRange3);
        return emit_opcode_reg_jump(source_offset, op, range_regs, target);
    }

    uint32_t CodeObjectBuilder::emit_for_iter_range(uint32_t source_offset,
                                                    Bytecode op,
                                                    uint32_t range_regs,
                                                    JumpTarget &target)
    {
        assert(op == Bytecode::ForIterRange1 ||
               op == Bytecode::ForIterRangeStep);
        return emit_opcode_reg_jump(source_offset, op, range_regs, target);
    }

    uint32_t CodeObjectBuilder::emit_binary_op(uint32_t source_offset,
                                               Bytecode op, uint32_t lhs_reg)
    {
        return emit_opcode_reg(source_offset, op, lhs_reg);
    }

    uint32_t CodeObjectBuilder::emit_binary_smi_op(uint32_t source_offset,
                                                   Bytecode op, int8_t rhs)
    {
        return emit_opcode_smi(source_offset, op, rhs);
    }

    uint32_t CodeObjectBuilder::emit_compare_op(uint32_t source_offset,
                                                Bytecode op, uint32_t lhs_reg)
    {
        return emit_opcode_reg(source_offset, op, lhs_reg);
    }

    uint32_t CodeObjectBuilder::emit_unary_op(uint32_t source_offset,
                                              Bytecode op)
    {
        return emit_opcode(source_offset, op);
    }

    uint32_t CodeObjectBuilder::emit_call_code_object(
        uint32_t source_offset, uint8_t code_object_idx,
        OutgoingArgReg first_arg_reg, uint8_t argc)
    {
        return emit_opcode_constant_idx_reg_argc(
            source_offset, Bytecode::CallCodeObject, code_object_idx,
            first_arg_reg, argc);
    }

    uint32_t CodeObjectBuilder::emit_call_native(uint32_t source_offset,
                                                 Bytecode op,
                                                 uint8_t target_idx)
    {
        assert(op == Bytecode::CallNative0 || op == Bytecode::CallNative1 ||
               op == Bytecode::CallNative2 || op == Bytecode::CallNative3);
        return emit_opcode_native_target_idx(source_offset, op, target_idx);
    }

    uint32_t CodeObjectBuilder::emit_call_simple(uint32_t source_offset,
                                                 uint32_t callable_reg,
                                                 OutgoingArgReg first_arg_reg,
                                                 uint8_t argc)
    {
        uint32_t result =
            emplace_back(source_offset, uint8_t(Bytecode::CallSimple));
        uint8_t cache_idx = allocate_function_call_cache();
        emplace_back(source_offset, encode_reg(callable_reg));
        uint32_t first_arg_operand_offset = code_obj->code.size();
        emplace_back(source_offset, first_arg_reg.slot_offset);
        add_outgoing_arg_relocation(first_arg_operand_offset,
                                    first_arg_reg.slot_offset);
        emplace_back(source_offset, argc);
        emplace_back(source_offset, cache_idx);
        return result;
    }

    uint32_t CodeObjectBuilder::allocate_constant(Value val)
    {
        uint32_t idx = code_obj->constant_table.size();
        code_obj->constant_table.emplace_back(val);
        assert(idx < 256);
        return idx;
    }

    uint32_t
    CodeObjectBuilder::add_native_function_target(NativeFunctionTarget target)
    {
        uint32_t idx = code_obj->native_function_targets.size();
        code_obj->native_function_targets.push_back(target);
        assert(idx < 256);
        return idx;
    }

    CodeObject *CodeObjectBuilder::finalize()
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
        patch_outgoing_arg_relocations();
        finalized = true;
        return code_obj;
    }

    uint32_t CodeObjectBuilder::allocate_attribute_read_cache()
    {
        uint32_t idx = code_obj->attribute_read_caches.size();
        code_obj->attribute_read_caches.emplace_back();
        assert(idx < 256);
        return idx;
    }

    uint32_t CodeObjectBuilder::allocate_attribute_mutation_cache()
    {
        uint32_t idx = code_obj->attribute_mutation_caches.size();
        code_obj->attribute_mutation_caches.emplace_back();
        assert(idx < 256);
        return idx;
    }

    uint32_t CodeObjectBuilder::allocate_function_call_cache()
    {
        uint32_t idx = code_obj->function_call_caches.size();
        code_obj->function_call_caches.emplace_back();
        assert(idx < 256);
        return idx;
    }

    uint32_t CodeObjectBuilder::emplace_back(uint32_t source_offset, uint8_t c)
    {
        assert_not_finalized();
        uint32_t offset = code_obj->code.size();
        code_obj->code.push_back(c);
        code_obj->source_offsets.push_back(source_offset);
        return offset;
    }

    uint32_t CodeObjectBuilder::emit_opcode(uint32_t source_offset, Bytecode c)
    {
        assert(c != Bytecode::Invalid);
        return emplace_back(source_offset, uint8_t(c));
    }

    uint32_t CodeObjectBuilder::emit_opcode_smi(uint32_t source_offset,
                                                Bytecode c, int8_t smi)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, smi);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_constant_idx(uint32_t source_offset,
                                                         Bytecode c,
                                                         uint8_t constant_idx)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_constant_idx_reg(
        uint32_t source_offset, Bytecode c, uint8_t constant_idx, uint32_t reg)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, encode_reg(reg));
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_constant_idx_reg(
        uint32_t source_offset, Bytecode c, uint8_t constant_idx,
        OutgoingArgReg reg)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        uint32_t operand_offset = code_obj->code.size();
        emplace_back(source_offset, reg.slot_offset);
        add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_constant_idx_reg_argc(
        uint32_t source_offset, Bytecode c, uint8_t constant_idx,
        OutgoingArgReg reg, uint8_t argc)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, constant_idx);
        uint32_t operand_offset = code_obj->code.size();
        emplace_back(source_offset, reg.slot_offset);
        add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
        emplace_back(source_offset, argc);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_reg(uint32_t source_offset,
                                                Bytecode c, uint32_t reg)
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

    uint32_t CodeObjectBuilder::emit_opcode_reg(uint32_t source_offset,
                                                Bytecode c, OutgoingArgReg reg)
    {
        assert(c != Bytecode::Invalid);
        emplace_back(source_offset, uint8_t(c));
        uint32_t operand_offset = code_obj->code.size();
        emplace_back(source_offset, reg.slot_offset);
        add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
        return operand_offset - 1;
    }

    uint32_t CodeObjectBuilder::emit_opcode_reg_range(uint32_t source_offset,
                                                      Bytecode c, uint32_t reg,
                                                      uint8_t n_regs)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        emplace_back(source_offset, n_regs);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_reg_range(uint32_t source_offset,
                                                      Bytecode c,
                                                      OutgoingArgReg reg,
                                                      uint8_t n_regs)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        uint32_t operand_offset = code_obj->code.size();
        emplace_back(source_offset, reg.slot_offset);
        add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
        emplace_back(source_offset, n_regs);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_native_target_idx(
        uint32_t source_offset, Bytecode c, uint8_t target_idx)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, target_idx);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_reg_constant_idx_cache_idx(
        uint32_t source_offset, Bytecode c, uint32_t reg, uint8_t constant_idx,
        uint8_t cache_idx)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, cache_idx);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_reg_constant_idx_cache_idx_argc(
        uint32_t source_offset, Bytecode c, OutgoingArgReg reg,
        uint8_t constant_idx, uint8_t read_cache_idx, uint8_t call_cache_idx,
        uint8_t argc)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        uint32_t operand_offset = code_obj->code.size();
        emplace_back(source_offset, reg.slot_offset);
        add_outgoing_arg_relocation(operand_offset, reg.slot_offset);
        emplace_back(source_offset, constant_idx);
        emplace_back(source_offset, read_cache_idx);
        emplace_back(source_offset, call_cache_idx);
        emplace_back(source_offset, argc);
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_reg_reg(uint32_t source_offset,
                                                    Bytecode c,
                                                    uint32_t first_reg,
                                                    uint32_t second_reg)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(first_reg));
        emplace_back(source_offset, encode_reg(second_reg));
        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_reg_jump(uint32_t source_offset,
                                                     Bytecode c, uint32_t reg,
                                                     JumpTarget &target)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, encode_reg(reg));
        uint32_t pos = code_obj->code.size();
        emplace_back(source_offset, 0);
        emplace_back(source_offset, 0);
        target.add_relocation(pos);

        return result;
    }

    uint32_t CodeObjectBuilder::emit_opcode_uint32(uint32_t source_offset,
                                                   Bytecode c, uint32_t k)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        emplace_back(source_offset, (k >> 0) & 0xff);
        emplace_back(source_offset, (k >> 8) & 0xff);
        emplace_back(source_offset, (k >> 16) & 0xff);
        emplace_back(source_offset, (k >> 24) & 0xff);

        return result;
    }

    uint32_t CodeObjectBuilder::emit_jump(uint32_t source_offset, Bytecode c,
                                          JumpTarget &target)
    {
        assert(c != Bytecode::Invalid);
        uint32_t result = emplace_back(source_offset, uint8_t(c));
        uint32_t pos = code_obj->code.size();
        emplace_back(source_offset, 0);
        emplace_back(source_offset, 0);
        target.add_relocation(pos);

        return result;
    }

    void CodeObjectBuilder::set_int16(uint32_t pos, int16_t v)
    {
        code_obj->code[pos + 0] = (v >> 0) & 0xff;
        code_obj->code[pos + 1] = (v >> 8) & 0xff;
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

    void CodeObjectBuilder::patch_outgoing_arg_relocations()
    {
        for(const OutgoingArgRelocation &reloc: outgoing_arg_relocations)
        {
            set_encoded_reg(
                reloc.operand_offset,
                get_outgoing_arg_reg(code_obj->code[reloc.operand_offset]));
        }
    }

    void CodeObjectBuilder::add_outgoing_arg_relocation(
        uint32_t operand_offset, uint32_t outgoing_slot_offset)
    {
        assert(outgoing_slot_offset < 256);
        outgoing_arg_relocations.push_back({operand_offset});
        code_obj->n_outgoing_call_slots =
            std::max(code_obj->n_outgoing_call_slots,
                     round_up_to_abi_alignment(outgoing_slot_offset + 1));
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

    uint32_t CodeObjectBuilder::reserve_registers(uint32_t n_regs)
    {
        assert_not_finalized();
        sync_temporary_reg_base();
        uint32_t reg = temporary_reg;
        temporary_reg += n_regs;
        max_temporary_reg = std::max(max_temporary_reg, temporary_reg);
        return reg;
    }

    void CodeObjectBuilder::release_registers(uint32_t reg, uint32_t n_regs)
    {
        temporary_reg -= n_regs;
        assert(reg == temporary_reg);
    }
}  // namespace cl
