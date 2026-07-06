#include "runtime/thread_state.h"
#include "api/clover_entry.h"
#include "builtin_types/dict.h"
#include "builtin_types/module_object.h"
#include "bytecode/code_object.h"
#include "compiler/codegen.h"
#include "compiler/compilation_unit.h"
#include "compiler/parser.h"
#include "compiler/tokenizer.h"
#include "memory/heap_reclamation.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "runtime/exception_object.h"
#include "runtime/interpreter.h"
#include "runtime/runtime_helpers.h"
#include "runtime/virtual_machine.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace cl
{
    static_assert(sizeof(Value) == 8);
    static_assert(FrameHeaderSizeAboveFp * sizeof(Value) == 32);
    static_assert((FrameHeaderSizeAboveFp * sizeof(Value)) %
                      FrameAlignmentBytes ==
                  0);
    static_assert((FrameAlignmentBytes & (FrameAlignmentBytes - 1)) == 0);

    thread_local ThreadState *ThreadState::current_thread = nullptr;

    PendingException::PendingException()
        : object(Optional<TValue<Exception>>::none()),
          stop_iteration_value(Value::not_present())
    {
    }

    static Value *align_clover_frame_pointer_down(Value *fp)
    {
        uintptr_t address = reinterpret_cast<uintptr_t>(fp);
        address &= ~(FrameAlignmentBytes - 1);
        return reinterpret_cast<Value *>(address);
    }

    static Value *compute_clover_frame_sentinel(std::vector<Value> &stack)
    {
        Value *highest_fp_with_header =
            stack.data() + stack.size() - FrameHeaderSizeAboveFp;
        return align_clover_frame_pointer_down(highest_fp_with_header);
    }

    static void initialize_clover_frame_sentinel(Value *sentinel_fp)
    {
        sentinel_fp[FrameHeaderPreviousFpOffset] =
            encode_frame_payload_ptr(static_cast<Value *>(nullptr));
        sentinel_fp[FrameHeaderCompiledReturnPcOffset] =
            encode_frame_payload_ptr(static_cast<const uint8_t *>(nullptr));
        sentinel_fp[FrameHeaderReturnCodeObjectOffset].as.ptr = nullptr;
        sentinel_fp[FrameHeaderReturnPcOffset] =
            encode_frame_payload_ptr(static_cast<const uint8_t *>(nullptr));
    }

    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine),
          safepoint_requested_ptr(machine->safepoint_requested_ptr()),
          refcounted_heap(&machine->get_refcounted_global_heap(),
                          machine->safepoint_requested_ptr()),
          stack(1024 * 1024)
    {
        Value *sentinel_fp = compute_clover_frame_sentinel(stack);
        clover_frame_sentinel_ptr = sentinel_fp;
        initialize_clover_frame_sentinel(sentinel_fp);
        set_clover_frame_frontier(sentinel_fp);
        publish_safepoint_scan_record(sentinel_fp, Value::not_present());
        refresh_class_for_native_layout_cache();
    }

    void ThreadState::refresh_class_for_native_layout_cache()
    {
        for(size_t idx = 0; idx < class_for_native_layouts.size(); ++idx)
        {
            class_for_native_layouts[idx] = machine->class_for_native_layout(
                static_cast<NativeLayoutId>(idx));
        }
        cache_exact_dict_shapes(machine->exact_dict_string_key_shape(),
                                machine->exact_dict_general_shape());
    }

    void
    ThreadState::publish_safepoint_scan_record(Value *lowest_live_stack_slot,
                                               Value accumulator_or_not_present)
    {
        assert(lowest_live_stack_slot >= stack.data());
        assert(lowest_live_stack_slot <= clover_frame_sentinel_ptr);
        safepoint_scan_record_.lowest_live_stack_slot = lowest_live_stack_slot;
        safepoint_scan_record_.accumulator_or_not_present =
            accumulator_or_not_present;
    }

    void ThreadState::handle_safepoint(Value accumulator, Value *fp,
                                       const uint8_t *pc,
                                       CodeObject *code_object)
    {
        machine->run_safepoint_callback_for_testing(
            this, accumulator, fp, code_object,
            code_object->offset_for_interpreted_pc(pc), safepoint_scan_record_);
        NoActiveThreadScope no_active_thread;
        machine->complete_safepoint();
    }

    static Value *entry_frame_pointer(Value *caller_fp, CodeObject *code_object)
    {
        int32_t slots_above_entry_fp =
            std::max(FrameHeaderSizeAboveFp,
                     code_object->get_highest_occupied_frame_offset() + 1);
        return caller_fp - slots_above_entry_fp;
    }

    Value ThreadState::run_clovervm_code_object(CodeObject *obj)
    {
        ActivationScope activation_scope(this);
        Owned<TValue<Function>> function(this->make_object_value<Function>(
            TValue<CodeObject>::from_oop(obj),
            Optional<TValue<String>>::none()));
        return call_clovervm_function_with_args(function.value(), nullptr, 0);
    }

    static void set_clover_entry_adapter_parameter(CodeObject *adapter,
                                                   Value *adapter_fp,
                                                   uint32_t parameter_idx,
                                                   Value value)
    {
        adapter_fp[adapter->encode_reg(parameter_idx)] = value;
    }

    Value ThreadState::call_clovervm_function_with_args(
        TValue<Function> function, const Value *args, uint32_t n_args)
    {
        ActivationScope activation_scope(this);
        CodeObject *adapter =
            CL_TRY(machine->clover_function_entry_adapter(n_args));
        Value *caller_fp = clover_frame_frontier();
        Value *adapter_fp = entry_frame_pointer(caller_fp, adapter);
        adapter_fp[FrameHeaderPreviousFpOffset] =
            encode_frame_payload_ptr(caller_fp);

        set_clover_entry_adapter_parameter(adapter, adapter_fp, 0,
                                           function.raw_value());
        for(uint32_t arg_idx = 0; arg_idx < n_args; ++arg_idx)
        {
            set_clover_entry_adapter_parameter(adapter, adapter_fp, arg_idx + 1,
                                               args[arg_idx]);
        }

        set_clover_frame_frontier(adapter_fp);
        return run_interpreter(adapter_fp, adapter, 0, this);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function)
    {
        return call_clovervm_function_with_args(function, nullptr, 0);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0)
    {
        Value args[] = {arg0};
        return call_clovervm_function_with_args(function, args, 1);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1)
    {
        Value args[] = {arg0, arg1};
        return call_clovervm_function_with_args(function, args, 2);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1,
                                              Value arg2)
    {
        Value args[] = {arg0, arg1, arg2};
        return call_clovervm_function_with_args(function, args, 3);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1,
                                              Value arg2, Value arg3)
    {
        Value args[] = {arg0, arg1, arg2, arg3};
        return call_clovervm_function_with_args(function, args, 4);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1,
                                              Value arg2, Value arg3,
                                              Value arg4)
    {
        Value args[] = {arg0, arg1, arg2, arg3, arg4};
        return call_clovervm_function_with_args(function, args, 5);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1,
                                              Value arg2, Value arg3,
                                              Value arg4, Value arg5)
    {
        Value args[] = {arg0, arg1, arg2, arg3, arg4, arg5};
        return call_clovervm_function_with_args(function, args, 6);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1,
                                              Value arg2, Value arg3,
                                              Value arg4, Value arg5,
                                              Value arg6)
    {
        Value args[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6};
        return call_clovervm_function_with_args(function, args, 7);
    }

    Expected<TValue<SMI>> ThreadState::hash_value(Value value)
    {
        Value result = call_clovervm_function(
            machine->hash_value_helper_function(), value);
        if(result.is_exception_marker())
        {
            return Expected<TValue<SMI>>::propagate_exception();
        }
        assert(result.is_smi());
        return Expected<TValue<SMI>>::ok(
            TValue<SMI>::from_value_unchecked(result));
    }

    Expected<bool> ThreadState::test_equal(Value left, Value right)
    {
        Value result = call_clovervm_function(
            machine->test_equal_helper_function(), left, right);
        if(result.is_exception_marker())
        {
            return Expected<bool>::propagate_exception();
        }
        assert(result.is_bool());
        return Expected<bool>::ok(result == Value::True());
    }

    Value ThreadState::call_clovervm_method_with_args(Value receiver,
                                                      TValue<String> name,
                                                      const Value *args,
                                                      uint32_t n_args)
    {
        Value callable;
        Value self;
        if(!load_method(receiver, name, callable, self))
        {
            return set_pending_builtin_exception_none(L"AttributeError");
        }
        if(!can_convert_to<Function>(callable))
        {
            return set_pending_builtin_exception_string(
                L"TypeError", L"object is not callable");
        }

        bool has_self = !self.is_not_present();
        uint32_t total_args = n_args + (has_self ? 1 : 0);
        if(total_args > MaxCloverFunctionEntryAdapterArgs)
        {
            return set_pending_builtin_exception_string(
                L"SystemError",
                L"unsupported Clover method call adapter arity");
        }

        Value method_args[MaxCloverFunctionEntryAdapterArgs];
        uint32_t out_idx = 0;
        if(has_self)
        {
            method_args[out_idx++] = self;
        }
        for(uint32_t arg_idx = 0; arg_idx < n_args; ++arg_idx)
        {
            method_args[out_idx++] = args[arg_idx];
        }

        return call_clovervm_function_with_args(
            TValue<Function>::from_value_assumed(callable), method_args,
            total_args);
    }

    Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name)
    {
        return call_clovervm_method_with_args(receiver, name, nullptr, 0);
    }

    Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name,
                                            Value arg0)
    {
        Value args[] = {arg0};
        return call_clovervm_method_with_args(receiver, name, args, 1);
    }

    Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name,
                                            Value arg0, Value arg1)
    {
        Value args[] = {arg0, arg1};
        return call_clovervm_method_with_args(receiver, name, args, 2);
    }

    Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name,
                                            Value arg0, Value arg1, Value arg2)
    {
        Value args[] = {arg0, arg1, arg2};
        return call_clovervm_method_with_args(receiver, name, args, 3);
    }

    Value ThreadState::set_pending_exception_string(TValue<ClassObject> type,
                                                    TValue<String> message)
    {
        return set_pending_exception_object(
            make_exception_object(this, type, message));
    }

    Value ThreadState::set_pending_exception_string(TValue<ClassObject> type,
                                                    const wchar_t *message)
    {
        return set_pending_exception_string(
            type, machine->get_or_create_interned_string_value(message));
    }

    Value ThreadState::set_pending_exception_none(TValue<ClassObject> type)
    {
        return set_pending_exception_string(type, L"");
    }

    Value
    ThreadState::set_pending_builtin_exception_string(const wchar_t *type_name,
                                                      TValue<String> message)
    {
        return set_pending_exception_string(
            TValue<ClassObject>::from_oop(class_for_builtin_name(type_name)),
            message);
    }

    Value
    ThreadState::set_pending_builtin_exception_string(const wchar_t *type_name,
                                                      const wchar_t *message)
    {
        return set_pending_builtin_exception_string(type_name,
                                                    interned_string(message));
    }

    Value
    ThreadState::set_pending_builtin_exception_none(const wchar_t *type_name)
    {
        return set_pending_builtin_exception_string(type_name, L"");
    }

    Value ThreadState::set_pending_stop_iteration_value(Value value)
    {
        pending_exception.object = Optional<TValue<Exception>>::none();
        pending_exception.stop_iteration_value = value;
        pending_exception.kind = PendingExceptionKind::StopIteration;
        return Value::exception_marker();
    }

    Shape *ThreadState::shape_of_inline_value(Value value) const
    {
        return machine->shape_for_inline_value(value);
    }

    ClassObject *ThreadState::class_for_builtin_name(const wchar_t *name) const
    {
        TValue<String> name_value =
            machine->get_or_create_interned_string_value(std::wstring(name));
        Value value =
            machine->global_builtins_module().extract()->get_own_property(
                name_value);
        assert(value.is_ptr());
        assert(value.get_ptr<Object>()->native_layout_id() ==
               NativeLayoutId::ClassObject);
        return value.get_ptr<ClassObject>();
    }

    ModuleObject *ThreadState::make_module_object(TValue<String> name,
                                                  Value builtins, Value doc,
                                                  Value package, Value loader,
                                                  Value spec, Value file)
    {
        return make_object_raw<ModuleObject>(name, builtins, doc, package,
                                             loader, spec, file);
    }

    ModuleObject *ThreadState::make_main_module(Value file)
    {
        ActivationScope activation_scope(this);

        TValue<String> main_name =
            machine->get_or_create_interned_string_value(L"__main__");
        ModuleObject *module = make_module_object(
            main_name, machine->global_builtins_module().raw_value(),
            Value::None(), Value::None(), Value::None(), Value::None(), file);

        machine->imported_modules().extract()->set_item(
            main_name.raw_value(), Value::from_oop(module));
        return module;
    }

    Expected<CodeObject *> ThreadState::compile(const wchar_t *str,
                                                StartRule start_rule)
    {
        ModuleObject *module = make_main_module(Value::not_present());
        return compile_in_module(str, start_rule, module,
                                 LanguageMode::StandardsCompliant);
    }

    Expected<CodeObject *> ThreadState::compile(const wchar_t *str,
                                                StartRule start_rule,
                                                const wchar_t *main_file)
    {
        ActivationScope activation_scope(this);

        Value file =
            machine->get_or_create_interned_string_value(main_file).raw_value();
        ModuleObject *module = make_main_module(file);
        return compile_in_module(str, start_rule, module,
                                 LanguageMode::StandardsCompliant);
    }

    Expected<CodeObject *> ThreadState::compile_in_module(
        const wchar_t *str, StartRule start_rule, ModuleObject *module,
        LanguageMode language_mode,
        CompileContinuationInfo *compile_continuation_info)
    {
        ActivationScope activation_scope(this);

        if(compile_continuation_info != nullptr)
        {
            *compile_continuation_info = CompileContinuationInfo{};
        }
        CompilationUnit input(str);
        TokenVector tv = CL_TRY(tokenize(input));
        AstVector av =
            CL_TRY(parse(*machine, tv, start_rule, compile_continuation_info));
        ModuleResultMode result_mode = start_rule == StartRule::Interactive
                                           ? ModuleResultMode::Interactive
                                           : ModuleResultMode::File;
        return codegen_module_in_module(av, module, language_mode, result_mode);
    }

    void ThreadState::add_to_active_zero_count_table_if_needed(HeapObject *obj)
    {
        ThreadState *ts = ThreadState::get_active();
        ts->add_to_zero_count_table_if_needed(obj);
    }

    void ThreadState::add_to_zero_count_table_if_needed(HeapObject *obj)
    {
        assert(obj != nullptr);
        assert((reinterpret_cast<uintptr_t>(obj) & value_ptr_mask) ==
               value_refcounted_ptr_tag);
        assert(obj->lifecycle_state != HeapLifecycleState::Reclaiming);
        assert(obj->lifecycle_state != HeapLifecycleState::Dead);
        if(obj->lifecycle_state == HeapLifecycleState::InZct)
        {
            return;
        }

        assert(obj->lifecycle_state == HeapLifecycleState::Normal);
        assert(obj->refcount == 0);
        obj->lifecycle_state = HeapLifecycleState::InZct;
        zero_count_table.push_back(obj);
    }

    void ThreadState::adopt_reclamation_state_from(ThreadState &child)
    {
        assert(this != &child);
        assert(machine == child.machine);
#ifndef NDEBUG
        for(HeapObject *obj: child.zero_count_table)
        {
            assert(std::find(zero_count_table.begin(), zero_count_table.end(),
                             obj) == zero_count_table.end());
        }
#endif

        zero_count_table.insert(
            zero_count_table.end(),
            std::make_move_iterator(child.zero_count_table.begin()),
            std::make_move_iterator(child.zero_count_table.end()));
        child.zero_count_table.clear();
        refcounted_heap.adopt_epoch_state_from(child.refcounted_heap);
    }

    bool
    ThreadState::zero_count_table_contains_for_testing(HeapObject *obj) const
    {
        return std::find(zero_count_table.begin(), zero_count_table.end(),
                         obj) != zero_count_table.end();
    }

}  // namespace cl
