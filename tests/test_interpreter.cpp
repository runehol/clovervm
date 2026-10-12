#include "builtin_types/bigint.h"
#include "builtin_types/dict.h"
#include "builtin_types/float.h"
#include "builtin_types/list.h"
#include "builtin_types/list_iterator.h"
#include "builtin_types/module_object.h"
#include "builtin_types/range_iterator.h"
#include "builtin_types/slice.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "builtin_types/tuple_iterator.h"
#include "bytecode/code_object_builder.h"
#include "bytecode/code_object_print.h"
#include "compiler/codegen.h"
#include "compiler/compilation_unit.h"
#include "compiler/parser.h"
#include "compiler/scope.h"
#include "compiler/token_print.h"
#include "compiler/tokenizer.h"
#include "import_system/module_global.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/instance.h"
#include "object_model/native_function.h"
#include "object_model/shape.h"
#include "object_model/slot_dict.h"
#include "object_model/typed_value.h"
#include "object_model/value_string.h"
#include "runtime/exception_object.h"
#include "runtime/interpreter.h"
#include "runtime/operator_walk.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include "test_helpers.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>

using namespace cl;

static constexpr int64_t kMinSmi = -288230376151711744LL;

static bool clover_frame_chain_reaches_terminated_root(Value *frontier,
                                                       Value *root,
                                                       uint32_t expected_count)
{
    Value *fp = frontier;
    uint32_t count = 0;
    for(uint32_t depth = 0; depth < 64 && fp != nullptr; ++depth)
    {
        ++count;
        if(fp == root)
        {
            return count == expected_count &&
                   fp[FrameHeaderPreviousFpOffset].as.ptr == nullptr &&
                   fp[FrameHeaderCompiledReturnPcOffset].as.ptr == nullptr &&
                   fp[FrameHeaderReturnCodeObjectOffset].as.ptr == nullptr &&
                   fp[FrameHeaderReturnPcOffset].as.ptr == nullptr;
        }
        fp = reinterpret_cast<Value *>(fp[FrameHeaderPreviousFpOffset].as.ptr);
    }
    return false;
}

struct CapturedStdoutRun
{
    Value return_value;
    std::wstring stdout_text;
};

static CapturedStdoutRun run_file_with_captured_stdout(const wchar_t *source)
{
    test::VmTestContext test_context;
    FILE *output = std::tmpfile();
    assert(output != nullptr);
    test_context.vm().set_stdout_file(output);

    Value return_value = test_context.run_file(source);
    std::fflush(output);
    std::rewind(output);

    std::wstring stdout_text;
    wint_t ch = 0;
    while((ch = std::fgetwc(output)) != WEOF)
    {
        stdout_text.push_back(static_cast<wchar_t>(ch));
    }
    std::fclose(output);

    return CapturedStdoutRun{return_value, stdout_text};
}

static std::wstring cl_test_string_to_wstring(TValue<String> string)
{
    String *str = string.extract();
    return std::wstring(str->data, size_t(str->count.extract()));
}

static void expect_thread_python_error(ThreadState *thread,
                                       const wchar_t *expected_type_name,
                                       const wchar_t *expected_message)
{
    ASSERT_TRUE(thread->has_pending_exception());
    if(std::wstring(expected_type_name) == L"StopIteration" &&
       thread->pending_exception_kind() == PendingExceptionKind::StopIteration)
    {
        EXPECT_EQ(std::wstring(L""), std::wstring(expected_message));
        return;
    }
    ASSERT_EQ(PendingExceptionKind::Object, thread->pending_exception_kind());
    TValue<Exception> exception = thread->pending_exception_object();
    EXPECT_EQ(std::wstring(expected_type_name),
              cl_test_string_to_wstring(
                  exception.extract()->get_shape()->get_class()->get_name()));
    EXPECT_EQ(std::wstring(expected_message),
              cl_test_string_to_wstring(
                  cl::test::native_exception_message(exception)));
}

static void expect_python_error(const wchar_t *source,
                                const wchar_t *expected_type_name,
                                const wchar_t *expected_message)
{
    test::FileRunner file_runner(source);
    EXPECT_TRUE(file_runner.return_value.is_exception_marker());
    expect_thread_python_error(file_runner.test_context().thread(),
                               expected_type_name, expected_message);
}

static void expect_string_value(Value actual, const wchar_t *expected)
{
    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(expected,
                 string_as_wchar_t(TValue<String>::from_value_assumed(actual)));
}

static int64_t g_next_counter = 0;
static Value *g_native_frame_frontier_seen = nullptr;
static Value *g_expected_clover_frame_sentinel = nullptr;
static uint32_t g_weave_frontier_checks = 0;

static Value native_next_counter(ThreadState *thread)
{
    return Value::from_smi(g_next_counter++);
}

static Value native_zero(ThreadState *thread) { return Value::from_smi(17); }

static Value native_sum7(ThreadState *thread, Value arg0, Value arg1,
                         Value arg2, Value arg3, Value arg4, Value arg5,
                         Value arg6)
{
    return Value::from_smi(arg0.get_smi() + arg1.get_smi() + arg2.get_smi() +
                           arg3.get_smi() + arg4.get_smi() + arg5.get_smi() +
                           arg6.get_smi());
}

static Value native_frame_frontier_result(ThreadState *thread, int64_t result)
{
    g_native_frame_frontier_seen = thread->clover_frame_frontier();
    return Value::from_smi(g_native_frame_frontier_seen != nullptr ? result
                                                                   : 0);
}

static Value native_frame_frontier0(ThreadState *thread)
{
    return native_frame_frontier_result(thread, 1);
}

static Value native_frame_frontier1(ThreadState *thread, Value arg0)
{
    return native_frame_frontier_result(thread,
                                        arg0 == Value::from_smi(10) ? 2 : 0);
}

static Value native_frame_frontier2(ThreadState *thread, Value arg0, Value arg1)
{
    return native_frame_frontier_result(
        thread,
        arg0 == Value::from_smi(10) && arg1 == Value::from_smi(20) ? 4 : 0);
}

static Value native_frame_frontier3(ThreadState *thread, Value arg0, Value arg1,
                                    Value arg2)
{
    return native_frame_frontier_result(
        thread, arg0 == Value::from_smi(10) && arg1 == Value::from_smi(20) &&
                        arg2 == Value::from_smi(30)
                    ? 8
                    : 0);
}

static void expect_current_frontier_reaches_initial(uint32_t expected_count)
{
    ThreadState *thread = active_thread();
    Value *frontier = thread->clover_frame_frontier();
    EXPECT_NE(nullptr, frontier);
    EXPECT_TRUE(clover_frame_chain_reaches_terminated_root(
        frontier, g_expected_clover_frame_sentinel, expected_count));
    ++g_weave_frontier_checks;
}

static Value native_weave_inner(ThreadState *thread)
{
    expect_current_frontier_reaches_initial(8);
    return Value::from_smi(23);
}

static Value native_weave_outer(ThreadState *thread, Value inner_function)
{
    expect_current_frontier_reaches_initial(5);
    Value result = active_thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(inner_function));
    expect_current_frontier_reaches_initial(5);
    if(result.is_exception_marker())
    {
        return result;
    }
    return Value::from_smi(result.get_smi() + 19);
}

static Value native_increment(ThreadState *thread, Value value)
{
    if(!value.is_smi())
    {
        throw std::runtime_error("native_increment expected a smi");
    }
    return Value::from_smi(value.get_smi() + 1);
}

static Value native_add(ThreadState *thread, Value left, Value right)
{
    if(!left.is_smi() || !right.is_smi())
    {
        throw std::runtime_error("native_add expected smi arguments");
    }
    return Value::from_smi(left.get_smi() + right.get_smi());
}

static Value native_add_three(ThreadState *thread, Value left, Value right,
                              Value third)
{
    if(!left.is_smi() || !right.is_smi() || !third.is_smi())
    {
        throw std::runtime_error("native_add_three expected smi arguments");
    }
    return Value::from_smi(left.get_smi() + right.get_smi() + third.get_smi());
}

static Value native_stop_iteration_with_value(ThreadState *thread)
{
    return active_thread()->set_pending_stop_iteration_value(
        Value::from_smi(123));
}

static Value native_marker_without_pending_exception(ThreadState *thread)
{
    return Value::exception_marker();
}

static Value native_base_exception_with_message(ThreadState *thread)
{
    ClassObject *cls =
        active_thread()->class_for_native_layout(NativeLayoutId::Exception);
    return active_thread()->set_pending_exception_string(
        TValue<ClassObject>::from_oop(cls), L"boom");
}

static void *g_every_safepoint_reclamation_target_address = nullptr;
static uint64_t g_every_safepoint_reclamation_valid_objects = 0;

static Value
native_large_tuple_for_every_safepoint_reclamation(ThreadState *thread)
{
    size_t tuple_size = LargeAllocationSize / sizeof(Value);
    return active_thread()->make_object_value<Tuple>(tuple_size).raw_value();
}

static Value
native_capture_every_safepoint_reclamation_target(ThreadState *thread,
                                                  Value value)
{
    assert(value.is_ptr());
    assert(value.get_ptr<Object>()->native_layout_id() ==
           NativeLayoutId::Tuple);
    GlobalHeap &heap = thread->get_machine()->get_refcounted_global_heap();
    g_every_safepoint_reclamation_target_address = value.as.ptr;
    g_every_safepoint_reclamation_valid_objects =
        heap.count_valid_objects_slow();
    assert(heap.has_slab_for_address_for_testing(value.as.ptr));
    return Value::from_smi(0);
}

static Value native_every_safepoint_reclamation_ping(ThreadState *thread)
{
    return Value::from_smi(1);
}

template <typename T>
static void store_global_to_module_for_test(test::VmTestContext &test_context,
                                            CodeObject *code_object,
                                            const wchar_t *name, T value)
{
    TValue<String> name_value(
        test_context.vm().get_or_create_interned_string_value(name));
    ASSERT_TRUE(
        store_module_global(code_object->get_defining_module().extract(),
                            name_value, value.raw_value()));
}

static void store_global_to_module_for_test(test::VmTestContext &test_context,
                                            CodeObject *code_object,
                                            const wchar_t *name,
                                            Expected<TValue<Function>> value)
{
    ASSERT_TRUE(value.has_value());
    store_global_to_module_for_test(test_context, code_object, name,
                                    value.value());
}

static Value load_global_from_module_for_test(CodeObject *code_object,
                                              TValue<String> name)
{
    return load_module_global(code_object->get_defining_module().extract(),
                              name);
}

static Value make_test_function(test::VmTestContext &test_context,
                                const wchar_t *name, const wchar_t *source)
{
    CodeObject *code_object = test_context.compile_file(source);
    (void)test_context.thread()->run_clovervm_code_object(code_object);

    TValue<String> name_value(
        test_context.vm().get_or_create_interned_string_value(name));
    Value function_value =
        load_global_from_module_for_test(code_object, name_value);
    assert(function_value.is_ptr());
    assert(function_value.get_ptr<Object>()->native_layout_id() ==
           NativeLayoutId::Function);
    return function_value;
}

static CodeObject *make_raise_unwind_code(test::VmTestContext &test_context,
                                          Value raised)
{
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"<raise-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t constant_idx = builder.allocate_constant(raised).value();
    builder.emit_lda_constant(0, uint8_t(constant_idx)).value();
    builder.emit_raise_unwind(0).value();
    builder.emit_return(0).value();
    return builder.finalize().value();
}

static CodeObject *
make_truthiness_conversion_code(test::VmTestContext &test_context,
                                Bytecode truthiness_opcode, Value value)
{
    assert(truthiness_opcode == Bytecode::ToBool ||
           truthiness_opcode == Bytecode::ToBoolNot);
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<truthiness-conversion-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t constant_idx = builder.allocate_constant(value).value();
    builder.emit_lda_constant(0, uint8_t(constant_idx)).value();
    if(truthiness_opcode == Bytecode::ToBool)
    {
        builder.emit_to_bool(0).value();
    }
    else
    {
        builder.emit_to_bool_not(0).value();
    }
    builder.emit_return(0).value();
    return builder.finalize().value();
}

static Value run_truthiness_conversion(test::VmTestContext &test_context,
                                       Bytecode truthiness_opcode, Value value)
{
    CodeObject *code_object =
        make_truthiness_conversion_code(test_context, truthiness_opcode, value);
    return test_context.thread()->run_clovervm_code_object(code_object);
}

static Value *prepare_native_return_wrapper_frame(ThreadState *thread)
{
    Value *caller_fp = thread->clover_frame_frontier();
    Value *wrapper_fp = caller_fp - FrameHeaderSizeAboveFp;
    wrapper_fp[FrameHeaderPreviousFpOffset].as.ptr =
        reinterpret_cast<Object *>(caller_fp);
    thread->set_clover_frame_frontier(wrapper_fp);
    return wrapper_fp;
}

static Value *prepare_clover_function_entry_adapter_frame(ThreadState *thread,
                                                          CodeObject *adapter,
                                                          Value callable,
                                                          const Value *args,
                                                          uint32_t n_args)
{
    Value *caller_fp = thread->clover_frame_frontier();
    Value *adapter_fp = caller_fp - 64;
    adapter_fp[FrameHeaderPreviousFpOffset].as.ptr =
        reinterpret_cast<Object *>(caller_fp);

    auto set_parameter = [adapter_fp, adapter](uint32_t parameter_idx,
                                               Value value) {
        int32_t fp_offset = int32_t(adapter->get_padded_n_parameters()) - 1 +
                            FrameHeaderSizeAboveFp - int32_t(parameter_idx);
        adapter_fp[fp_offset] = value;
    };
    set_parameter(0, callable);
    for(uint32_t arg_idx = 0; arg_idx < n_args; ++arg_idx)
    {
        set_parameter(arg_idx + 1, args[arg_idx]);
    }

    thread->set_clover_frame_frontier(adapter_fp);
    return adapter_fp;
}

static CodeObject *make_return_to_native_code(test::VmTestContext &test_context)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<return-to-native-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    builder.emit_lda_smi(0, 42).value();
    builder.emit_return_to_native(0).value();
    return builder.finalize().value();
}

static CodeObject *
make_return_exception_marker_to_native_code(test::VmTestContext &test_context)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<return-exception-marker-to-native-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    builder.emit_return_exception_marker_to_native(0).value();
    return builder.finalize().value();
}

static Value run_is_instance_of_known_class(test::VmTestContext &test_context,
                                            Value value, ClassObject *cls)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<is-instance-of-known-class-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t value_idx = builder.allocate_constant(value).value();
    uint32_t class_idx =
        builder.allocate_constant(Value::from_oop(cls)).value();
    builder.emit_lda_constant(0, uint8_t(value_idx)).value();
    builder.emit_is_instance_of_known_class(0, uint8_t(class_idx)).value();
    builder.emit_return(0).value();
    CodeObject *code_obj = builder.finalize().value();
    return test_context.thread()->run_clovervm_code_object(code_obj);
}

TEST(Interpreter, is_instance_of_known_class_handles_inline_values)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    EXPECT_EQ(Value::True(),
              run_is_instance_of_known_class(test_context, Value::from_smi(42),
                                             test_context.vm().int_class()));
    EXPECT_EQ(Value::True(),
              run_is_instance_of_known_class(test_context, Value::True(),
                                             test_context.vm().int_class()));
    EXPECT_EQ(Value::False(),
              run_is_instance_of_known_class(test_context, Value::from_smi(42),
                                             test_context.vm().str_class()));
}

TEST(Interpreter, is_instance_of_known_class_follows_mro)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        test_context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> sibling_name(
        test_context.vm().get_or_create_interned_string_value(L"Sibling"));
    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    ClassObject *child = test_context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);
    ClassObject *sibling =
        test_context.thread()->make_internal_raw<ClassObject>(
            sibling_name, 2, test_context.vm().object_class(),
            NativeLayoutId::Instance);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(child);

    EXPECT_EQ(Value::True(), run_is_instance_of_known_class(
                                 test_context, Value::from_oop(obj), child));
    EXPECT_EQ(Value::True(), run_is_instance_of_known_class(
                                 test_context, Value::from_oop(obj), base));
    EXPECT_EQ(Value::True(),
              run_is_instance_of_known_class(test_context, Value::from_oop(obj),
                                             test_context.vm().object_class()));
    EXPECT_EQ(Value::False(), run_is_instance_of_known_class(
                                  test_context, Value::from_oop(obj), sibling));
}

TEST(Interpreter, assert_statement_raises_assertion_error)
{
    expect_python_error(L"assert False\n", L"AssertionError", L"");
}

TEST(Interpreter, assert_statement_raises_assertion_error_with_message)
{
    expect_python_error(L"assert False, \"basic math is broken\"\n",
                        L"AssertionError", L"basic math is broken");
}

TEST(Interpreter, assert_statement_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    assert False\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"AssertionError", L"");
}

TEST(Interpreter, to_bool_converts_immediate_truthiness)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::False(),
              run_truthiness_conversion(test_context, Bytecode::ToBool,
                                        Value::from_smi(0)));
    EXPECT_EQ(Value::True(),
              run_truthiness_conversion(test_context, Bytecode::ToBool,
                                        Value::from_smi(7)));
    EXPECT_EQ(Value::False(),
              run_truthiness_conversion(test_context, Bytecode::ToBool,
                                        Value::False()));
    EXPECT_EQ(Value::True(),
              run_truthiness_conversion(test_context, Bytecode::ToBool,
                                        Value::True()));
    EXPECT_EQ(Value::False(),
              run_truthiness_conversion(test_context, Bytecode::ToBool,
                                        Value::None()));
}

TEST(Interpreter, to_bool_not_inverts_immediate_truthiness)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::True(),
              run_truthiness_conversion(test_context, Bytecode::ToBoolNot,
                                        Value::from_smi(0)));
    EXPECT_EQ(Value::False(),
              run_truthiness_conversion(test_context, Bytecode::ToBoolNot,
                                        Value::from_smi(7)));
    EXPECT_EQ(Value::True(),
              run_truthiness_conversion(test_context, Bytecode::ToBoolNot,
                                        Value::False()));
    EXPECT_EQ(Value::False(),
              run_truthiness_conversion(test_context, Bytecode::ToBoolNot,
                                        Value::True()));
    EXPECT_EQ(Value::True(),
              run_truthiness_conversion(test_context, Bytecode::ToBoolNot,
                                        Value::None()));
}

TEST(Interpreter, truthiness_conversion_handles_float_truthiness)
{
    test::VmTestContext test_context;

    Value zero =
        test_context.thread()->make_object_value<Float>(0.0).raw_value();
    Value one =
        test_context.thread()->make_object_value<Float>(1.0).raw_value();

    EXPECT_EQ(Value::False(),
              run_truthiness_conversion(test_context, Bytecode::ToBool, zero));
    EXPECT_EQ(Value::True(),
              run_truthiness_conversion(test_context, Bytecode::ToBool, one));
    EXPECT_EQ(Value::True(), run_truthiness_conversion(
                                 test_context, Bytecode::ToBoolNot, zero));
    EXPECT_EQ(Value::False(), run_truthiness_conversion(
                                  test_context, Bytecode::ToBoolNot, one));
}

TEST(Interpreter, truthiness_conversion_rejects_unsupported_pointer_truthiness)
{
    test::VmTestContext test_context;

    Value string_value = test_context.vm()
                             .get_or_create_interned_string_value(L"truthy")
                             .raw_value();
    Value result =
        run_truthiness_conversion(test_context, Bytecode::ToBool, string_value);
    EXPECT_TRUE(result.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"TypeError",
                               L"unsupported truthiness for object");
}

TEST(Interpreter, truthiness_conversion_opcodes_are_printed)
{
    test::VmTestContext test_context;
    CodeObject *code_object = make_truthiness_conversion_code(
        test_context, Bytecode::ToBoolNot, Value::from_smi(1));

    std::string printed = fmt::to_string(*code_object);
    EXPECT_NE(std::string::npos, printed.find("ToBoolNot"));
}

TEST(Interpreter, raise_statement_raises_exception_class)
{
    expect_python_error(L"raise ValueError\n", L"ValueError", L"");
}

TEST(Interpreter, raise_statement_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    raise ValueError\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"ValueError", L"");
}

TEST(Interpreter, run_clovervm_code_object_returns_pending_exception)
{
    expect_python_error(L"raise ValueError\n", L"ValueError", L"");
}

TEST(Interpreter, trace_interpreter_instructions_prints_executed_bytecode)
{
    test::VmTestContext test_context;
    test_context.thread()->set_trace_interpreter_instructions(true);

    testing::internal::CaptureStderr();
    Value actual = test_context.run_file(L"def f():\n"
                                         L"    return 7\n"
                                         L"f()\n");
    std::string trace = testing::internal::GetCapturedStderr();

    EXPECT_EQ(Value::from_smi(7), actual);
    EXPECT_NE(std::string::npos, trace.find(">>> enter f\n"));
    EXPECT_NE(std::string::npos, trace.find("CallPositional"));
    EXPECT_NE(std::string::npos, trace.find("LdaSmi 7"));
    EXPECT_NE(std::string::npos, trace.find("Return"));
}

TEST(Interpreter, try_bare_except_handler_can_raise)
{
    expect_python_error(L"try:\n"
                        L"    raise ValueError\n"
                        L"except:\n"
                        L"    raise TypeError\n",
                        L"TypeError", L"");
}

TEST(Interpreter, del_global_removes_binding)
{
    expect_python_error(L"value = 7\n"
                        L"del value\n"
                        L"value\n",
                        L"NameError", L"name 'value' is not defined");
}

TEST(Interpreter, del_missing_global_raises_name_error)
{
    expect_python_error(L"del missing\n", L"NameError",
                        L"name 'missing' is not defined");
}

TEST(Interpreter, module_without_explicit_builtins_uses_default_builtins)
{
    test::FileRunner file_runner(L"range(1)\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::RangeIterator,
              actual.get_ptr<Object>()->native_layout_id());
}

TEST(Interpreter, del_local_variable_removes_binding)
{
    expect_python_error(L"def clear(value):\n"
                        L"    del value\n"
                        L"    return value\n"
                        L"clear(7)\n",
                        L"NameError", L"name 'value' is not defined");
}

TEST(Interpreter, loop_local_read_after_delete_can_still_raise)
{
    expect_python_error(L"def f(n):\n"
                        L"    x = 1\n"
                        L"    for i in range(n):\n"
                        L"        x\n"
                        L"        del x\n"
                        L"    return 0\n"
                        L"f(2)\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, while_condition_after_delete_can_still_raise)
{
    expect_python_error(L"def f():\n"
                        L"    x = 1\n"
                        L"    while x:\n"
                        L"        del x\n"
                        L"    return 0\n"
                        L"f()\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, loop_break_after_delete_ignores_unreachable_assignment)
{
    expect_python_error(L"def f():\n"
                        L"    x = 1\n"
                        L"    while x:\n"
                        L"        del x\n"
                        L"        break\n"
                        L"        x = 1\n"
                        L"    return x\n"
                        L"f()\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, loop_continue_after_delete_ignores_unreachable_assignment)
{
    expect_python_error(L"def f():\n"
                        L"    x = 1\n"
                        L"    for i in range(2):\n"
                        L"        if i:\n"
                        L"            return x\n"
                        L"        del x\n"
                        L"        continue\n"
                        L"        x = 1\n"
                        L"    return 0\n"
                        L"f()\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, try_handler_entry_accounts_for_protected_prefix_delete)
{
    expect_python_error(L"def boom():\n"
                        L"    1 / 0\n"
                        L"def f():\n"
                        L"    x = 1\n"
                        L"    try:\n"
                        L"        del x\n"
                        L"        boom()\n"
                        L"        x = 1\n"
                        L"    except ZeroDivisionError:\n"
                        L"        pass\n"
                        L"    return x\n"
                        L"f()\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, finally_entry_accounts_for_protected_prefix_delete)
{
    expect_python_error(L"def boom():\n"
                        L"    1 / 0\n"
                        L"def f():\n"
                        L"    x = 1\n"
                        L"    try:\n"
                        L"        del x\n"
                        L"        boom()\n"
                        L"        x = 1\n"
                        L"    finally:\n"
                        L"        x\n"
                        L"f()\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, finally_entry_accounts_for_handler_prefix_delete)
{
    expect_python_error(L"def f():\n"
                        L"    x = 1\n"
                        L"    try:\n"
                        L"        raise ValueError\n"
                        L"    except ValueError:\n"
                        L"        del x\n"
                        L"    finally:\n"
                        L"        return x\n"
                        L"f()\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, with_suppressed_exception_accounts_for_body_prefix_delete)
{
    expect_python_error(L"class Manager:\n"
                        L"    def __enter__(self):\n"
                        L"        return self\n"
                        L"    def __exit__(self, typ, exc, tb):\n"
                        L"        return True\n"
                        L"def f():\n"
                        L"    x = 1\n"
                        L"    with Manager():\n"
                        L"        del x\n"
                        L"        raise ValueError\n"
                        L"        x = 1\n"
                        L"    return x\n"
                        L"f()\n",
                        L"NameError", L"name 'x' is not defined");
}

TEST(Interpreter, assert_message_expression_gets_scope_analysis)
{
    expect_python_error(L"def f():\n"
                        L"    assert False, missing\n"
                        L"    missing = 1\n"
                        L"f()\n",
                        L"NameError", L"name 'missing' is not defined");
}

TEST(Interpreter, del_missing_local_raises_name_error)
{
    expect_python_error(L"def clear():\n"
                        L"    del value\n"
                        L"clear()\n",
                        L"NameError", L"name 'value' is not defined");
}

TEST(Interpreter, local_read_before_assignment_raises_name_error)
{
    expect_python_error(L"def read_before_write():\n"
                        L"    value\n"
                        L"    value = 7\n"
                        L"read_before_write()\n",
                        L"NameError", L"name 'value' is not defined");
}

TEST(Interpreter, conditional_local_assignment_raises_on_missing_path)
{
    expect_python_error(L"def maybe_write(flag):\n"
                        L"    if flag:\n"
                        L"        value = 7\n"
                        L"    return value\n"
                        L"maybe_write(False)\n",
                        L"NameError", L"name 'value' is not defined");
}

TEST(Interpreter, function_wrong_arity)
{
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"f()\n",
                        L"TypeError", L"wrong number of arguments");
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"f(1, 2)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, function_wrong_arity_unwinds_nested_frames)
{
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"def fail():\n"
                        L"    f()\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, function_keyword_call_rejects_keyword_only_as_positional)
{
    expect_python_error(L"def f(a, *, b=2):\n"
                        L"    return a + b\n"
                        L"f(1, 2)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, function_keyword_call_rejects_missing_required_keyword_only)
{
    expect_python_error(L"def f(a=1, *, b, c=3):\n"
                        L"    return a + b + c\n"
                        L"f()\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, function_call_positional_rejects_required_keyword_only)
{
    expect_python_error(L"def f(a=1, *, b, c=3):\n"
                        L"    return a + b + c\n"
                        L"f(2)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, function_positional_only_rejects_keyword_argument)
{
    expect_python_error(L"def f(a, /):\n"
                        L"    return a\n"
                        L"f(a=1)\n",
                        L"TypeError", L"invalid keyword argument");
}

TEST(Interpreter, function_kwargs_rejects_duplicate_formal_fill)
{
    expect_python_error(L"def f(a, **kwargs):\n"
                        L"    return kwargs\n"
                        L"f(1, a=2)\n",
                        L"TypeError", L"invalid keyword argument");
}

TEST(Interpreter, function_kwargs_does_not_absorb_extra_positionals)
{
    expect_python_error(L"def f(a, **kwargs):\n"
                        L"    return kwargs\n"
                        L"f(1, 2)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, class_constructor_custom_new_with_init_has_no_thunk_yet)
{
    expect_python_error(L"class C:\n"
                        L"    def __new__(cls):\n"
                        L"        return 42\n"
                        L"    def __init__(self):\n"
                        L"        pass\n"
                        L"C()\n",
                        L"TypeError", L"object is not callable");
}

TEST(Interpreter, int_constructor_converts_large_string_to_bigint)
{
    test::VmTestContext context;

    Value positive = context.run_file(L"int('288230376151711744')\n");
    ASSERT_TRUE(can_convert_to<BigInt>(positive));
    expect_string_value(context.run_file(L"str(int('288230376151711744'))\n"),
                        L"288230376151711744");

    Value negative = context.run_file(L"int('-288230376151711745')\n");
    ASSERT_TRUE(can_convert_to<BigInt>(negative));
    expect_string_value(context.run_file(L"str(int('-288230376151711745'))\n"),
                        L"-288230376151711745");
}

TEST(Interpreter, integer_literal_can_be_bigint)
{
    test::VmTestContext context;

    Value literal = context.run_file(L"288230376151711744\n");
    ASSERT_TRUE(can_convert_to<BigInt>(literal));
    expect_string_value(context.run_file(L"str(288230376151711744)\n"),
                        L"288230376151711744");
    EXPECT_EQ(Value::True(),
              context.run_file(
                  L"288_230_376_151_711_744 == int('288230376151711744')\n"));
}

TEST(Interpreter, negative_integer_literal_can_be_bigint)
{
    test::VmTestContext context;

    Value literal = context.run_file(L"-288230376151711745\n");
    ASSERT_TRUE(can_convert_to<BigInt>(literal));
    expect_string_value(context.run_file(L"str(-288230376151711745)\n"),
                        L"-288230376151711745");
}

TEST(Interpreter, int_constructor_rejects_invalid_string)
{
    expect_python_error(L"int('')\n", L"ValueError",
                        L"invalid literal for int()");
    expect_python_error(L"int('12x')\n", L"ValueError",
                        L"invalid literal for int()");
    expect_python_error(L"int('1__2')\n", L"ValueError",
                        L"invalid literal for int()");
}

TEST(Interpreter, int_constructor_rejects_unsupported_value)
{
    expect_python_error(L"class C:\n"
                        L"    pass\n"
                        L"int(C())\n",
                        L"TypeError",
                        L"int conversion is only implemented for int, bool and "
                        L"str");
}

TEST(Interpreter, function_keyword_call_rejects_duplicate_formal_fill)
{
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"f(1, a=2)\n",
                        L"TypeError", L"invalid keyword argument");
}

TEST(Interpreter, function_keyword_call_rejects_unexpected_keyword)
{
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"f(b=1)\n",
                        L"TypeError", L"invalid keyword argument");
}

TEST(Interpreter, function_keyword_call_rejects_missing_required)
{
    expect_python_error(L"def f(a, b=2):\n"
                        L"    return a + b\n"
                        L"f(b=1)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, function_varargs_still_requires_positional_arguments)
{
    expect_python_error(L"def f(a, *args):\n"
                        L"    return a\n"
                        L"f()\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, class_body_assignment_becomes_class_member)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    value = 7\n");
    (void)test_context.thread()->run_clovervm_code_object(code_obj);

    Value cls_value = load_global_from_module_for_test(code_obj, cls_name);
    ASSERT_TRUE(cls_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              cls_value.get_ptr<Object>()->native_layout_id());
    ClassObject *cls = cls_value.get_ptr<ClassObject>();
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(cls), value_name));

    Shape *shape = cls->get_shape();
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    StorageLocation value_location =
        shape->resolve_present_property(value_name);
    ASSERT_TRUE(value_location.is_found());
    EXPECT_EQ(StorageKind::Inline, value_location.kind);
    EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_count),
              value_location.physical_idx);
    EXPECT_EQ(Value::from_smi(7), cls->read_storage_location(value_location));
    constexpr uint32_t class_metadata_descriptor_count =
        ClassObject::class_metadata_slot_count + 2;
    ASSERT_EQ(class_metadata_descriptor_count + 1, shape->present_count());
    EXPECT_STREQ(L"value",
                 shape->get_property_name(class_metadata_descriptor_count)
                     .extract()
                     ->data);
}

TEST(Interpreter, class_body_attributes_preserve_shape_insertion_order)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> first_name(
        test_context.vm().get_or_create_interned_string_value(L"first"));
    TValue<String> second_name(
        test_context.vm().get_or_create_interned_string_value(L"second"));
    TValue<String> third_name(
        test_context.vm().get_or_create_interned_string_value(L"third"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    first = 1\n"
                                                     L"    second = 2\n"
                                                     L"    third = 3\n");
    (void)test_context.thread()->run_clovervm_code_object(code_obj);

    Value cls_value = load_global_from_module_for_test(code_obj, cls_name);
    ASSERT_TRUE(cls_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              cls_value.get_ptr<Object>()->native_layout_id());
    ClassObject *cls = cls_value.get_ptr<ClassObject>();

    TValue<String> names[] = {first_name, second_name, third_name};
    constexpr uint32_t class_metadata_descriptor_count =
        ClassObject::class_metadata_slot_count + 2;
    for(uint32_t idx = 0; idx < 3; ++idx)
    {
        EXPECT_STREQ(string_as_wchar_t(names[idx]),
                     string_as_wchar_t(cls->get_shape()->get_property_name(
                         class_metadata_descriptor_count + idx)));

        StorageLocation location =
            cls->get_shape()->resolve_present_property(names[idx]);
        ASSERT_TRUE(location.is_found());
        EXPECT_EQ(StorageKind::Inline, location.kind);
        EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_count + idx),
                  location.physical_idx);
        EXPECT_EQ(Value::from_smi(idx + 1),
                  cls->read_storage_location(location));
    }
}

TEST(Interpreter, class_body_readonly_metadata_store_is_rejected)
{
    expect_python_error(L"class Cls:\n"
                        L"    __name__ = 1\n",
                        L"TypeError", L"cannot set read-only class attribute");
}

TEST(Interpreter, set_name_notification_is_explicitly_unsupported)
{
    expect_python_error(L"class Descriptor:\n"
                        L"    def __set_name__(self, owner, name):\n"
                        L"        self.owner = owner\n"
                        L"class Owner:\n"
                        L"    field = Descriptor()\n",
                        L"TypeError",
                        L"__set_name__ notifications are not implemented yet");
}

TEST(Interpreter, store_attr_add_own_property_initializes_instance_slots)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"obj = Cls()\n"
                                                     L"obj.first = 1\n"
                                                     L"obj.second = 2\n"
                                                     L"obj\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Instance,
              actual.get_ptr<Object>()->native_layout_id());
    Instance *instance = actual.get_ptr<Instance>();

    EXPECT_EQ(2u, instance->native_layout_aux_count_value());
    EXPECT_EQ(Value::from_smi(1), instance->inline_slot_base()[0]);
    EXPECT_EQ(Value::from_smi(2), instance->inline_slot_base()[1]);
}

TEST(Interpreter, class_constructor_rejects_non_none_init_return)
{
    expect_python_error(L"class Cls:\n"
                        L"    def __init__(self):\n"
                        L"        return 1\n"
                        L"Cls()\n",
                        L"TypeError",
                        L"__init__ should return None, not a value");
}

TEST(Interpreter, class_constructor_non_none_init_return_unwinds_nested_frames)
{
    expect_python_error(L"class Cls:\n"
                        L"    def __init__(self):\n"
                        L"        return 1\n"
                        L"def fail():\n"
                        L"    Cls()\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError",
                        L"__init__ should return None, not a value");
}

TEST(Interpreter, str_constructor_rejects_non_string_dunder_str_return)
{
    expect_python_error(L"class C:\n"
                        L"    def __str__(self):\n"
                        L"        return 123\n"
                        L"str(C())\n",
                        L"TypeError", L"__str__ returned non-str");
}

TEST(Interpreter, float_literal_values)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"1\n"));

    auto expect_float_literal = [&](const wchar_t *source, double expected) {
        Value actual = test_context.run_file(source);
        ASSERT_TRUE(can_convert_to<Float>(actual));
        EXPECT_DOUBLE_EQ(expected, actual.get_ptr<Float>()->value());
    };

    expect_float_literal(L"1.0\n", 1.0);
    expect_float_literal(L"1.\n", 1.0);
    expect_float_literal(L".5\n", 0.5);
    expect_float_literal(L"1e3\n", 1000.0);
    expect_float_literal(L"1E3\n", 1000.0);
    expect_float_literal(L"1.2e-3\n", 0.0012);
    expect_float_literal(L"1_2.3_4\n", 12.34);

    Value huge_actual = test_context.run_file(L"1e1000\n");
    ASSERT_TRUE(can_convert_to<Float>(huge_actual));
    EXPECT_TRUE(std::isinf(huge_actual.get_ptr<Float>()->value()));
    EXPECT_FALSE(std::signbit(huge_actual.get_ptr<Float>()->value()));

    Value negative_huge_actual = test_context.run_file(L"-1e1000\n");
    ASSERT_TRUE(can_convert_to<Float>(negative_huge_actual));
    EXPECT_TRUE(std::isinf(negative_huge_actual.get_ptr<Float>()->value()));
    EXPECT_TRUE(std::signbit(negative_huge_actual.get_ptr<Float>()->value()));

    Value tiny_actual = test_context.run_file(L"1e-1000\n");
    ASSERT_TRUE(can_convert_to<Float>(tiny_actual));
    EXPECT_EQ(0.0, tiny_actual.get_ptr<Float>()->value());
    EXPECT_FALSE(std::signbit(tiny_actual.get_ptr<Float>()->value()));

    Value negative_tiny_actual = test_context.run_file(L"-1e-1000\n");
    ASSERT_TRUE(can_convert_to<Float>(negative_tiny_actual));
    EXPECT_EQ(0.0, negative_tiny_actual.get_ptr<Float>()->value());
    EXPECT_TRUE(std::signbit(negative_tiny_actual.get_ptr<Float>()->value()));

    std::wstring tiny_decimal_source = L"0." + std::wstring(400, L'0') + L"1\n";
    Value tiny_decimal_actual =
        test_context.run_file(tiny_decimal_source.c_str());
    ASSERT_TRUE(can_convert_to<Float>(tiny_decimal_actual));
    EXPECT_EQ(0.0, tiny_decimal_actual.get_ptr<Float>()->value());
    EXPECT_FALSE(std::signbit(tiny_decimal_actual.get_ptr<Float>()->value()));
}

TEST(Interpreter, builtin_pow_with_modulo_rejects_binary_only_pow)
{
    expect_python_error(L"class Left:\n"
                        L"    def __pow__(self, other):\n"
                        L"        return 42\n"
                        L"pow(Left(), 3, 5)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, integer_power_errors)
{
    expect_python_error(L"e = -1\n"
                        L"0 ** e\n",
                        L"ZeroDivisionError", L"zero to a negative power");
    expect_python_error(L"base = 1 << 2000\n"
                        L"e = -1\n"
                        L"base ** e\n",
                        L"OverflowError", L"int too large to convert to float");
    expect_python_error(L"pow(2, 3, 0)\n", L"ValueError",
                        L"pow() 3rd argument cannot be 0");
    expect_python_error(L"pow(2, -1, 4)\n", L"ValueError",
                        L"base is not invertible for the given modulus");
}

TEST(Interpreter, true_division_reports_zero_division)
{
    expect_python_error(L"1 / 0\n", L"ZeroDivisionError", L"division by zero");
    expect_python_error(L"1 / 0.0\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"1 / -0.0\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"(1.0).__truediv__(0.0)\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"(0.0).__rtruediv__(1.0)\n", L"ZeroDivisionError",
                        L"division by zero");
}

TEST(Interpreter, true_division_reports_unsupported_operands)
{
    expect_python_error(L"\"a\" / 1\n", L"TypeError",
                        L"unsupported operand type(s) for /");
    expect_python_error(L"1 / \"a\"\n", L"TypeError",
                        L"unsupported operand type(s) for /");
}

TEST(Interpreter, floor_division_reports_errors)
{
    expect_python_error(L"1 // 0\n", L"ZeroDivisionError", L"division by zero");
    expect_python_error(L"int('18446744073709551616') // 0\n",
                        L"ZeroDivisionError", L"division by zero");
    expect_python_error(L"1 // 0.0\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"(1.0).__floordiv__(0.0)\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"(0.0).__rfloordiv__(1.0)\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"\"a\" // 1\n", L"TypeError",
                        L"unsupported operand type(s) for //");
    expect_python_error(L"1 // \"a\"\n", L"TypeError",
                        L"unsupported operand type(s) for //");
}

TEST(Interpreter, modulo_reports_errors)
{
    expect_python_error(L"1 % 0\n", L"ZeroDivisionError", L"division by zero");
    expect_python_error(L"int('18446744073709551616') % 0\n",
                        L"ZeroDivisionError", L"division by zero");
    expect_python_error(L"1 % 0.0\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"(1.0).__mod__(0.0)\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"(0.0).__rmod__(1.0)\n", L"ZeroDivisionError",
                        L"division by zero");
    expect_python_error(L"\"a\" % 1\n", L"TypeError",
                        L"unsupported operand type(s) for %");
    expect_python_error(L"1 % \"a\"\n", L"TypeError",
                        L"unsupported operand type(s) for %");
}

TEST(Interpreter, arithmetic_reports_unsupported_operands)
{
    expect_python_error(L"\"a\" + 1\n", L"TypeError",
                        L"unsupported operand type(s) for +");
    expect_python_error(L"1 - \"a\"\n", L"TypeError",
                        L"unsupported operand type(s) for -");
    expect_python_error(L"\"a\" * 2\n", L"TypeError",
                        L"unsupported operand type(s) for *");
    expect_python_error(L"-\"a\"\n", L"TypeError",
                        L"unsupported operand type for unary arithmetic");
    expect_python_error(L"\"a\" < 1\n", L"TypeError",
                        L"unsupported operand type(s) for comparison");
}

TEST(Interpreter, unary_float_handler_has_registered_metadata)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"negate"));
    CodeObject *code_obj = test_context.compile_file(L"def negate(value):\n"
                                                     L"    return -value\n"
                                                     L"negate(1.5)\n"
                                                     L"negate(2.5)\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(can_convert_to<Float>(actual));
    EXPECT_DOUBLE_EQ(-2.5, actual.get_ptr<Float>()->value());

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_FALSE(cache.trusted_handler.is_null());

    std::optional<TrustedHandlerMetadata> metadata =
        test_context.vm().trusted_handler_metadata(
            cache.trusted_handler.target(TrustedHandlerArity::Unary));
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(TrustedHandlerArity::Unary, metadata->arity);
    EXPECT_EQ(TrustedHandlerEffects::Allocate, metadata->effects);
    EXPECT_EQ(TrustedHandlerSemantics::Neg, metadata->semantics);
}

TEST(Interpreter, sequence_search_methods_have_local_equality_caches)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code =
        test_context.compile_file(L"class Equal:\n"
                                  L"    def __eq__(self, other):\n"
                                  L"        return True\n"
                                  L"item = Equal()\n"
                                  L"needle = Equal()\n"
                                  L"for i in range(2):\n"
                                  L"    assert [item].count(needle) == 1\n"
                                  L"    assert [item].index(needle) == 0\n"
                                  L"    assert [item].remove(needle) is None\n"
                                  L"    assert (item,).count(needle) == 1\n"
                                  L"    assert (item,).index(needle) == 0\n");
    ASSERT_FALSE(test_context.thread()
                     ->run_clovervm_code_object(code)
                     .is_exception_marker());
    Value cls =
        load_global_from_module_for_test(code, interned_string(L"Equal"));
    Value equality = load_attr(cls, interned_string(L"__eq__"));
    ASSERT_TRUE(can_convert_to<Function>(equality));

    for(ClassObject *sequence_class:
        {test_context.vm().list_class(), test_context.vm().tuple_class()})
    {
        for(const wchar_t *name: {L"count", L"index", L"remove"})
        {
            if(sequence_class == test_context.vm().tuple_class() &&
               std::wcscmp(name, L"remove") == 0)
            {
                continue;
            }
            Value method = load_attr(Value::from_oop(sequence_class),
                                     interned_string(name));
            ASSERT_TRUE(can_convert_to<Function>(method));
            CodeObject *method_code =
                method.get_ptr<Function>()->code_object.extract();
            EXPECT_EQ(nullptr, method_code->trusted_handler_resolver);
            bool found_equality_cache = false;
            for(const OperatorInlineCache &cache:
                method_code->inline_caches.operator_caches)
            {
                if(cache.function == equality.get_ptr<Function>())
                {
                    found_equality_cache = true;
                    EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
                    EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
                }
            }
            EXPECT_TRUE(found_equality_cache);
        }
    }
    for(const wchar_t *name:
        {L"_clover_list_count", L"_clover_list_index", L"_clover_list_remove",
         L"_clover_tuple_count", L"_clover_tuple_index", L"_clover_sys"})
    {
        EXPECT_TRUE(test_context.vm()
                        .global_builtins_module()
                        .extract()
                        ->get_own_property(interned_string(name))
                        .is_not_present());
    }
}

TEST(Interpreter, sequence_search_methods_survive_builtin_rebinding)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
import builtins
List = list
Tuple = tuple
Int = int
IsInstance = isinstance
TypeErrorClass = TypeError
ValueErrorClass = ValueError

def restore():
    builtins.list = List
    builtins.tuple = Tuple
    builtins.int = Int
    builtins.isinstance = IsInstance
    builtins.TypeError = TypeErrorClass
    builtins.ValueError = ValueErrorClass

# Both normal results and helper-raised errors must use retained dependencies.
builtins.list = None
builtins.tuple = None
builtins.int = None
builtins.isinstance = None
builtins.TypeError = None
builtins.ValueError = None
try:
    for values in ([1, 2, 1], (1, 2, 1)):
        assert values.count(1) == 2
        assert values.index(1, True) == 2
        assert values.index(2, True).__class__ is Int
        try:
            values.index(9)
        except ValueErrorClass:
            pass
        else:
            raise AssertionError
        try:
            values.index(1, "bad")
        except TypeErrorClass:
            pass
        else:
            raise AssertionError
    values = [1, 2, 1]
    assert values.remove(1) is None
    assert values[0] == 2
    assert values.count(1) == 1
    try:
        values.remove(9)
    except ValueErrorClass:
        pass
    else:
        raise AssertionError
    for method in (List.count, List.index, List.remove, Tuple.count, Tuple.index):
        try:
            method(1, 1)
        except TypeErrorClass:
            pass
        else:
            raise AssertionError
finally:
    restore()
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, sequence_search_methods_survive_equality_callback_rebinding)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
import builtins
List = list
Tuple = tuple
Int = int
IsInstance = isinstance
TypeErrorClass = TypeError
ValueErrorClass = ValueError

def restore():
    builtins.list = List
    builtins.tuple = Tuple
    builtins.int = Int
    builtins.isinstance = IsInstance
    builtins.TypeError = TypeErrorClass
    builtins.ValueError = ValueErrorClass

class Rebind:
    def __init__(self, matches): self.matches = matches
    def __eq__(self, other):
        builtins.list = None
        builtins.tuple = None
        builtins.int = None
        builtins.isinstance = None
        builtins.TypeError = None
        builtins.ValueError = None
        return self.matches

match = Rebind(True)
miss = Rebind(False)
for values in ([match, match], (match, match)):
    try:
        assert values.count(42) == 2
    finally:
        restore()
    try:
        # True becomes an int index even if equality rebinds the int name.
        index = values.index(42, True)
        assert index == 1
        assert index.__class__ is Int
    finally:
        restore()
for values in ([miss], (miss,)):
    try:
        try:
            values.index(42)
        except ValueErrorClass:
            pass
        else:
            raise AssertionError
    finally:
        restore()
values = [match, 7]
try:
    assert values.remove(42) is None
    assert len(values) == 1
    assert values[0] == 7
finally:
    restore()
values = [miss]
try:
    try:
        values.remove(42)
    except ValueErrorClass:
        pass
    else:
        raise AssertionError
finally:
    restore()
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, tuple_equality_values_and_identity_shortcuts)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
import math
assert () == ()
assert not (() != ())
assert (1, "x", (2, 3)) == (1.0, "x", (2, 3))
assert not ((1, "x") != (1.0, "x"))
assert (1, 2) != (1, 3)
assert (1,) != (1, 2)
assert () != (1,)
assert not ((1,) == [1])
assert (1,) != [1]
assert tuple.__eq__((1,), [1]) is NotImplemented
assert tuple.__ne__((1,), [1]) is NotImplemented
nan = math.nan
assert (nan,) == (nan,)
assert not ((nan,) != (nan,))
class Bomb:
    def __eq__(self, other): raise AssertionError
    def __ne__(self, other): raise AssertionError
x = Bomb()
assert (x,) == (x,)
assert not ((x,) != (x,))
assert tuple.__eq__((x,), (x,)) is True
assert tuple.__ne__((x,), (x,)) is False
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, tuple_equality_callbacks_and_truthiness)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
log = []
class Element:
    def __init__(self, label, answer):
        self.label = label
        self.answer = answer
    def __eq__(self, other):
        log.append(self.label)
        return self.answer
    def __ne__(self, other): raise AssertionError("element ne called")
a = Element("first", 1)
b = Element("second", 0)
c = Element("unreached", True)
other = object()
for mode in range(2):
    log.clear()
    if mode == 0: assert not ((a, b, c) == (other, other, other))
    else: assert (a, b, c) != (other, other, other)
    assert len(log) == 2
    assert log[0] == "first"
    assert log[1] == "second"
log.clear()
assert (a,) != (other, other)
assert len(log) == 1
class Raises:
    def __eq__(self, other): raise ValueError("element equality")
for mode in range(2):
    try:
        if mode == 0: (Raises(),) == (other, other)
        else: (Raises(),) != (other, other)
    except ValueError as e: assert str(e) == "element equality"
    else: raise AssertionError
class BadTruth:
    def __bool__(self): raise AssertionError("custom truthiness is deferred")
class BadElement:
    def __eq__(self, other): return BadTruth()
try: (BadElement(),) != (other,)
except TypeError as e: assert str(e) == "unsupported truthiness for object"
else: raise AssertionError
class Reflected:
    def __eq__(self, other): return "reflected equality"
    def __ne__(self, other): return "reflected inequality"
assert (() == Reflected()) == "reflected equality"
assert (() != Reflected()) == "reflected inequality"
class Declines:
    def __eq__(self, other): return NotImplemented
assert (Declines(),) != (Declines(),)
class Reentrant:
    def __eq__(self, other): return (1, (2,)) == (1.0, (2.0,))
assert (Reentrant(),) == (other,)
for answer in (None, False, 0, 0.0):
    assert (Element("false", answer),) != (other,)
for answer in (True, 1, -1, 1.0):
    assert (Element("true", answer),) == (other,)
for answer in ("", [], (), 2**100):
    try: (Element("unsupported", answer),) == (other,)
    except TypeError as e: assert str(e) == "unsupported truthiness for object"
    else: raise AssertionError
left = None
right = None
class DropsReferences:
    def __eq__(self, other):
        global left, right
        left = None
        right = None
        return (1, (2,)) == (1.0, (2.0,))
left = (DropsReferences(), 7)
right = (object(), 7.0)
assert left == right
assert left is None
assert right is None
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_equality_values_callbacks_and_subclasses)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
import math
assert [] == []
assert not ([] != [])
assert [1, "x", [2, 3]] == [1.0, "x", [2, 3]]
assert [1, 2] != [1, 3]
assert [1] != [1, 2]
assert not ([1] == (1,))
assert [1] != (1,)
assert list.__eq__([1], (1,)) is NotImplemented
assert list.__ne__([1], (1,)) is NotImplemented
nan = math.nan
assert [nan] == [nan]
class Bomb:
    def __eq__(self, other): raise AssertionError
    def __ne__(self, other): raise AssertionError
x = Bomb()
assert [x] == [x]
assert not ([x] != [x])
assert [x] != [object(), object()]
assert not ([x] == [object(), object()])
log = []
class Element:
    def __init__(self, label, answer):
        self.label = label
        self.answer = answer
    def __eq__(self, other):
        log.append(self.label)
        return self.answer
    def __ne__(self, other): raise AssertionError
for mode in range(2):
    log.clear()
    left = [Element("first", 1), Element("second", 0), Bomb()]
    right = [object(), object(), object()]
    if mode == 0: assert not (left == right)
    else: assert left != right
    assert log == ["first", "second"]
class Raises:
    def __eq__(self, other): raise ValueError("list equality")
for mode in range(2):
    try:
        if mode == 0: [Raises()] == [object()]
        else: [Raises()] != [object()]
    except ValueError as e: assert str(e) == "list equality"
    else: raise AssertionError
class CustomTruth:
    def __bool__(self): raise AssertionError
class Unsupported:
    def __eq__(self, other): return CustomTruth()
try: [Unsupported()] == [object()]
except TypeError as e: assert str(e) == "unsupported truthiness for object"
else: raise AssertionError
class Reflected:
    def __eq__(self, other): return "reflected equality"
    def __ne__(self, other): return "reflected inequality"
assert ([] == Reflected()) == "reflected equality"
assert ([] != Reflected()) == "reflected inequality"
class Plain(list):
    def __len__(self): raise AssertionError
    def __getitem__(self, i): raise AssertionError
    def __iter__(self): raise AssertionError
class Derived(Plain):
    def __eq__(self, other): return "derived equality"
    def __ne__(self, other): return "derived inequality"
class OnlyEq(Plain):
    def __eq__(self, other): return False
plain = Plain([1, 2])
derived = Derived([1, 2])
only_eq = OnlyEq([1, 2])
assert plain == [1, 2]
assert not (plain != [1, 2])
assert list.__eq__(derived, plain) is True
assert list.__ne__(derived, plain) is False
assert (plain == derived) == "derived equality"
assert (plain != derived) == "derived inequality"
assert not (only_eq == plain)
assert not (only_eq != plain)
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_equality_mutation_and_reference_lifetime)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
left = []
right = []
class Clear:
    def __init__(self, answer): self.answer = answer
    def __eq__(self, other):
        left.clear()
        right.clear()
        return self.answer
for answer in (True, False):
    for mode in range(2):
        left = [Clear(answer), 1]
        right = [object(), 2]
        if mode == 0: assert left == right
        else: assert not (left != right)
        assert left == []
        assert right == []
class ClearOne:
    def __eq__(self, other):
        left.clear()
        return True
left = [ClearOne()]
right = [object()]
assert left != right
class Grow:
    def __eq__(self, other):
        left.append(1)
        right.append(2)
        return True
left = [Grow()]
right = [object()]
assert left != right
class Replace:
    def __eq__(self, other):
        left[1] = 7
        right[1] = 7.0
        return True
left = [Replace(), 1]
right = [object(), 2]
assert left == right
class Delete:
    def __eq__(self, other):
        del left[0]
        del right[0]
        return True
left = [Delete(), 1]
right = [object(), 2]
assert left == right
class DropOwners:
    def __eq__(self, other):
        global left, right
        left = None
        right = None
        return [1, [2]] == [1.0, [2.0]]
left = [DropOwners(), 3]
right = [object(), 3.0]
assert left == right
assert left is None
assert right is None
# Both removed elements remain alive until the callback finishes.
class Retain:
    def __init__(self): self.value = 9
    def __eq__(self, other):
        left.clear()
        right.clear()
        assert self.value == 9
        assert other.value == 9
        return True
left = [Retain()]
right = [Retain()]
assert left == right
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_equality_survives_callback_rebinding)
{
    expect_python_error(L"list.__eq__(1, ())\n", L"TypeError",
                        L"list.__eq__ expects a list receiver");
    expect_python_error(L"list.__ne__(1, ())\n", L"TypeError",
                        L"list.__ne__ expects a list receiver");
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
import builtins
list_type = list
type_error = TypeError
not_implemented = NotImplemented
original_isinstance = isinstance
class Rebind:
    def __eq__(self, other):
        builtins.list = None
        builtins.isinstance = None
        builtins.TypeError = None
        builtins.NotImplemented = None
        return True
try:
    assert [Rebind(), 1] == [object(), 1.0]
    assert not ([Rebind(), 1] != [object(), 1.0])
    assert list_type.__eq__([], ()) is not_implemented
    assert list_type.__ne__([], ()) is not_implemented
    for method in (list_type.__eq__, list_type.__ne__):
        try: method(1, ())
        except type_error: pass
        else: raise AssertionError
finally:
    builtins.list = list_type
    builtins.isinstance = original_isinstance
    builtins.TypeError = type_error
    builtins.NotImplemented = not_implemented
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_equality_methods_have_warmed_element_caches)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = context.run_file(LR"py(
class Element:
    def __eq__(self, other): return True
left = [Element()]
right = [Element()]
for i in range(4):
    assert left == right
    assert not (left != right)
def replacement(self, other): return False
Element.__eq__ = replacement
assert left != right
assert not (left == right)
)py");
    ASSERT_FALSE(result.is_exception_marker());
    Value equality = load_attr(Value::from_oop(context.vm().list_class()),
                               interned_string(L"__eq__"));
    ASSERT_TRUE(can_convert_to<Function>(equality));
    CodeObject *code = equality.get_ptr<Function>()->code_object.extract();
    EXPECT_EQ(nullptr, code->trusted_handler_resolver);
    bool found_element_cache = false;
    for(const OperatorInlineCache &cache: code->inline_caches.operator_caches)
    {
        if(cache.function != nullptr)
        {
            found_element_cache = true;
            EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
            EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
        }
    }
    EXPECT_TRUE(found_element_cache);
    for(const wchar_t *name: {L"_clover_list_eq", L"_clover_list_ne"})
    {
        EXPECT_TRUE(context.vm()
                        .global_builtins_module()
                        .extract()
                        ->get_own_property(interned_string(name))
                        .is_not_present());
    }
}

TEST(Interpreter, tuple_equality_survives_callback_rebinding)
{
    expect_python_error(L"tuple.__eq__(1, ())\n", L"TypeError",
                        L"tuple.__eq__ expects a tuple receiver");
    expect_python_error(L"tuple.__ne__(1, ())\n", L"TypeError",
                        L"tuple.__ne__ expects a tuple receiver");
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
import builtins
tuple_type = tuple
type_error = TypeError
not_implemented = NotImplemented
original_isinstance = isinstance
class Rebind:
    def __eq__(self, other):
        builtins.tuple = None
        builtins.isinstance = None
        builtins.TypeError = None
        builtins.NotImplemented = None
        return True
try:
    assert (Rebind(), 1) == (object(), 1.0)
    assert not ((Rebind(), 1) != (object(), 1.0))
    assert tuple_type.__eq__((), []) is not_implemented
    assert tuple_type.__ne__((), []) is not_implemented
    for method in (tuple_type.__eq__, tuple_type.__ne__):
        try: method(1, ())
        except type_error: pass
        else: raise AssertionError
finally:
    builtins.tuple = tuple_type
    builtins.isinstance = original_isinstance
    builtins.TypeError = type_error
    builtins.NotImplemented = not_implemented
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, tuple_equality_methods_have_warmed_element_caches)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = context.run_file(LR"py(
class Element:
    def __eq__(self, other): return True
left = (Element(),)
right = (Element(),)
for i in range(4):
    assert left == right
    assert not (left != right)
def replacement(self, other): return False
Element.__eq__ = replacement
assert left != right
assert not (left == right)
)py");
    ASSERT_FALSE(result.is_exception_marker());
    Value equality = load_attr(Value::from_oop(context.vm().tuple_class()),
                               interned_string(L"__eq__"));
    ASSERT_TRUE(can_convert_to<Function>(equality));
    CodeObject *code = equality.get_ptr<Function>()->code_object.extract();
    EXPECT_EQ(nullptr, code->trusted_handler_resolver);
    bool found_element_cache = false;
    for(const OperatorInlineCache &cache: code->inline_caches.operator_caches)
    {
        if(cache.function != nullptr)
        {
            found_element_cache = true;
            EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
            EXPECT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
        }
    }
    EXPECT_TRUE(found_element_cache);
    for(const wchar_t *name: {L"_clover_tuple_eq", L"_clover_tuple_ne"})
    {
        EXPECT_TRUE(context.vm()
                        .global_builtins_module()
                        .extract()
                        ->get_own_property(interned_string(name))
                        .is_not_present());
    }
}

TEST(Interpreter, tuple_equality_subclasses_preserve_payload_and_dispatch)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    CodeObject *classes = context.compile_file(LR"py(
class Plain(tuple):
    def __len__(self): raise AssertionError
    def __getitem__(self, index): raise AssertionError
    def __iter__(self): raise AssertionError
class Derived(Plain):
    def __eq__(self, other): return "derived equality"
    def __ne__(self, other): return "derived inequality"
class OnlyEq(Plain):
    def __eq__(self, other): return False
)py");
    ASSERT_FALSE(context.thread()
                     ->run_clovervm_code_object(classes)
                     .is_exception_marker());
    std::vector<Owned<TValue<Tuple>>> values;
    values.reserve(3);
    const wchar_t *class_names[] = {L"Plain", L"Derived", L"OnlyEq"};
    for(size_t idx = 0; idx < 3; ++idx)
    {
        Value cls = load_global_from_module_for_test(
            classes, interned_string(class_names[idx]));
        ASSERT_TRUE(can_convert_to<ClassObject>(cls));
        values.emplace_back(context.thread()->make_internal_value<Tuple>(
            cls.get_ptr<ClassObject>(), 2));
        values[idx].extract()->initialize_item_unchecked(0, Value::from_smi(1));
        values[idx].extract()->initialize_item_unchecked(1, Value::from_smi(2));
    }
    CodeObject *checks = context.compile_file(LR"py(
assert plain == (1, 2)
assert not (plain != (1, 2))
assert tuple.__eq__(derived, plain) is True
assert tuple.__ne__(derived, plain) is False
assert (plain == derived) == "derived equality"
assert (plain != derived) == "derived inequality"
assert not (only_eq == plain)
assert not (only_eq != plain)
assert tuple.__eq__(only_eq, plain) is True
assert tuple.__ne__(only_eq, plain) is False
def replacement(self, other): return "changed equality"
plain.__class__.__eq__ = replacement
assert (plain == (1, 2)) == "changed equality"
assert not (plain != (1, 2))
del plain.__class__.__eq__
assert plain == (1, 2)
)py");
    const wchar_t *value_names[] = {L"plain", L"derived", L"only_eq"};
    for(size_t idx = 0; idx < 3; ++idx)
    {
        store_global_to_module_for_test(context, checks, value_names[idx],
                                        values[idx].value());
    }
    ASSERT_FALSE(context.thread()
                     ->run_clovervm_code_object(checks)
                     .is_exception_marker());
}

TEST(Interpreter, sequence_search_methods_bypass_subclass_storage_overrides)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *classes =
        test_context.compile_file(L"class HostileList(list):\n"
                                  L"    def __len__(self):\n"
                                  L"        raise AssertionError\n"
                                  L"    def __getitem__(self, index):\n"
                                  L"        raise AssertionError\n"
                                  L"    def __iter__(self):\n"
                                  L"        raise AssertionError\n"
                                  L"    def __delitem__(self, index):\n"
                                  L"        raise AssertionError\n"
                                  L"class HostileTuple(tuple):\n"
                                  L"    def __len__(self):\n"
                                  L"        raise AssertionError\n"
                                  L"    def __getitem__(self, index):\n"
                                  L"        raise AssertionError\n"
                                  L"    def __iter__(self):\n"
                                  L"        raise AssertionError\n");
    ASSERT_FALSE(test_context.thread()
                     ->run_clovervm_code_object(classes)
                     .is_exception_marker());
    Value list_class = load_global_from_module_for_test(
        classes, interned_string(L"HostileList"));
    Value tuple_class = load_global_from_module_for_test(
        classes, interned_string(L"HostileTuple"));
    ASSERT_TRUE(can_convert_to<ClassObject>(list_class));
    ASSERT_TRUE(can_convert_to<ClassObject>(tuple_class));
    Owned<TValue<ClassObject>> native_list_class(
        TValue<ClassObject>::from_value_assumed(list_class));
    Owned<TValue<ClassObject>> native_tuple_class(
        TValue<ClassObject>::from_value_assumed(tuple_class));
    EXPECT_EQ(NativeLayoutId::List,
              native_list_class.extract()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::Tuple,
              native_tuple_class.extract()->instance_native_layout_id());
    EXPECT_EQ(0u,
              native_list_class.extract()->get_instance_inline_slot_capacity());
    EXPECT_EQ(
        0u, native_tuple_class.extract()->get_instance_inline_slot_capacity());
    Owned<TValue<List>> list(test_context.thread()->make_internal_value<List>(
        native_list_class.extract()));
    list.extract()->append(Value::from_smi(7));
    list.extract()->append(Value::from_smi(8));
    Owned<TValue<Tuple>> tuple(
        test_context.thread()->make_internal_value<Tuple>(
            native_tuple_class.extract(), 2));
    tuple.extract()->initialize_item_unchecked(0, Value::from_smi(7));
    tuple.extract()->initialize_item_unchecked(1, Value::from_smi(8));
    CodeObject *checks =
        test_context.compile_file(L"assert xs.count(7.0) == 1\n"
                                  L"assert xs.index(8.0) == 1\n"
                                  L"assert xs.remove(7.0) is None\n"
                                  L"assert list.__len__(xs) == 1\n"
                                  L"assert list.__getitem__(xs, 0) == 8\n"
                                  L"assert ys.count(7.0) == 1\n"
                                  L"assert ys.index(8.0) == 1\n");
    store_global_to_module_for_test(test_context, checks, L"xs", list.value());
    store_global_to_module_for_test(test_context, checks, L"ys", tuple.value());
    ASSERT_FALSE(test_context.thread()
                     ->run_clovervm_code_object(checks)
                     .is_exception_marker());
}

TEST(Interpreter, builtin_abs_caches_trusted_unary_handlers)
{
    for(const wchar_t *source:
        {L"abs(-(10**30))\nabs(-(10**31))\n", L"abs(-1.5)\nabs(-2.5)\n"})
    {
        SCOPED_TRACE(source);
        test::VmTestContext test_context;
        ThreadState::ActivationScope activation_scope(test_context.thread());
        Value actual = test_context.run_file(source);
        ASSERT_FALSE(actual.is_exception_marker());

        Value abs_function = test_context.vm()
                                 .global_builtins_module()
                                 .extract()
                                 ->get_own_property(interned_string(L"abs"));
        ASSERT_TRUE(can_convert_to<Function>(abs_function));
        CodeObject *abs_code =
            abs_function.get_ptr<Function>()->code_object.extract();
        ASSERT_EQ(1u, abs_code->inline_caches.operator_caches.size());
        const OperatorInlineCache &cache =
            abs_code->inline_caches.operator_caches[0];
        ASSERT_FALSE(cache.trusted_handler.is_null());

        std::optional<TrustedHandlerMetadata> metadata =
            test_context.vm().trusted_handler_metadata(
                cache.trusted_handler.target(TrustedHandlerArity::Unary));
        ASSERT_TRUE(metadata.has_value());
        EXPECT_EQ(TrustedHandlerArity::Unary, metadata->arity);
        EXPECT_TRUE(has_trusted_handler_effects(
            metadata->effects, TrustedHandlerEffects::Allocate));
        EXPECT_EQ(TrustedHandlerSemantics::Generic, metadata->semantics);
    }
}

TEST(Interpreter, int_abs_resolver_selects_trusted_smi_and_bool_handlers)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value method = load_attr(Value::from_oop(test_context.vm().int_class()),
                             interned_string(L"__abs__"));
    ASSERT_TRUE(can_convert_to<Function>(method));
    CodeObject *method_code = method.get_ptr<Function>()->code_object.extract();
    ASSERT_NE(nullptr, method_code->trusted_handler_resolver);
    for(Value value: {Value::from_smi(-3), Value::True(), Value::False()})
    {
        TrustedResolution resolution = method_code->trusted_handler_resolver(
            &test_context.vm(), ShapeKey::from_value(value),
            ShapeKey::from_value(Value::not_present()),
            TrustedHandlerOperandOrder::Normal, TrustedHandlerArity::Unary);
        ASSERT_TRUE(resolution.has_trusted_handler());
        EXPECT_EQ(TrustedHandlerArity::Unary, resolution.arity);
        Value result = resolution.unary(test_context.thread(), value);
        EXPECT_EQ(value.is_smi() ? Value::from_smi(3)
                                 : Value::from_smi(value == Value::True()),
                  result);
        ASSERT_TRUE(test_context.vm()
                        .trusted_handler_metadata(
                            erase_trusted_handler_target(resolution.unary))
                        .has_value());
    }
}

TEST(Interpreter, unary_positive_float_preserves_exact_identity)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"positive"));
    CodeObject *code_obj =
        test_context.compile_file(L"def positive(value):\n"
                                  L"    return +value\n"
                                  L"value = 4.5\n"
                                  L"assert positive(value) is value\n"
                                  L"assert positive(value) is value\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_FALSE(actual.is_exception_marker());

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_FALSE(cache.trusted_handler.is_null());

    std::optional<TrustedHandlerMetadata> metadata =
        test_context.vm().trusted_handler_metadata(
            cache.trusted_handler.target(TrustedHandlerArity::Unary));
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(TrustedHandlerArity::Unary, metadata->arity);
    EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
    EXPECT_EQ(TrustedHandlerSemantics::Pos, metadata->semantics);
}

TEST(Interpreter, membership_fallback_does_not_index_when_iter_exists)
{
    expect_python_error(L"class Sequence:\n"
                        L"    def __iter__(self):\n"
                        L"        raise ValueError\n"
                        L"    def __getitem__(self, idx):\n"
                        L"        return 'needle'\n"
                        L"'needle' in Sequence()\n",
                        L"ValueError", L"");
}

TEST(Interpreter, membership_cache_hit_ignores_needle_shape)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"contains"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Container:\n"
                                  L"    def __contains__(self, item):\n"
                                  L"        return True\n"
                                  L"def contains(container, needle):\n"
                                  L"    return needle in container\n"
                                  L"container = Container()\n"
                                  L"contains(container, 1)\n"
                                  L"contains(container, 'needle')\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::True(), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, membership_reports_unsupported_contains_truthiness)
{
    expect_python_error(L"class Container:\n"
                        L"    def __contains__(self, item):\n"
                        L"        return 'yes'\n"
                        L"1 in Container()\n",
                        L"TypeError", L"unsupported truthiness for object");
}

TEST(Interpreter, native_string_contains_rejects_non_string_needle)
{
    expect_python_error(L"'abc'.__contains__(1)\n", L"TypeError",
                        L"'in <string>' requires string as left operand");
}

TEST(Interpreter, native_contains_membership_uses_trusted_handler)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> dict_function_name(
        test_context.vm().get_or_create_interned_string_value(
            L"dict_contains"));
    CodeObject *dict_code =
        test_context.compile_file(L"def dict_contains(container, needle):\n"
                                  L"    return needle in container\n"
                                  L"d = {'x': 1}\n"
                                  L"dict_contains(d, 'x')\n"
                                  L"dict_contains(d, 'y')\n");
    EXPECT_EQ(Value::False(),
              test_context.thread()->run_clovervm_code_object(dict_code));

    Value dict_function_value =
        load_global_from_module_for_test(dict_code, dict_function_name);
    ASSERT_TRUE(can_convert_to<Function>(dict_function_value));
    CodeObject *dict_function_code =
        assume_convert_to<Function>(dict_function_value)->code_object.extract();
    ASSERT_EQ(1u, dict_function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &dict_cache =
        dict_function_code->inline_caches.operator_caches[0];
    EXPECT_FALSE(dict_cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, dict_cache.function);
    EXPECT_NE(nullptr, dict_cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(nullptr, dict_cache.operand_lookup_validity_cells[1]);

    TValue<String> str_function_name(
        test_context.vm().get_or_create_interned_string_value(L"str_contains"));
    CodeObject *str_code =
        test_context.compile_file(L"def str_contains(container, needle):\n"
                                  L"    return needle in container\n"
                                  L"str_contains('abc', 'b')\n"
                                  L"str_contains('abc', 1)\n");
    EXPECT_TRUE(test_context.thread()
                    ->run_clovervm_code_object(str_code)
                    .is_exception_marker());
    expect_thread_python_error(
        test_context.thread(), L"TypeError",
        L"'in <string>' requires string as left operand");

    Value str_function_value =
        load_global_from_module_for_test(str_code, str_function_name);
    ASSERT_TRUE(can_convert_to<Function>(str_function_value));
    CodeObject *str_function_code =
        assume_convert_to<Function>(str_function_value)->code_object.extract();
    ASSERT_EQ(1u, str_function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &str_cache =
        str_function_code->inline_caches.operator_caches[0];
    EXPECT_FALSE(str_cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, str_cache.function);
    EXPECT_NE(nullptr, str_cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(nullptr, str_cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_add_walk_uses_arithmetic_reflected_priority)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> left_name(
        test_context.vm().get_or_create_interned_string_value(L"left"));
    TValue<String> right_name(
        test_context.vm().get_or_create_interned_string_value(L"right"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Base:\n"
                                  L"    def __add__(self, other):\n"
                                  L"        return 3\n"
                                  L"class Derived(Base):\n"
                                  L"    def __radd__(self, other):\n"
                                  L"        return 7\n"
                                  L"left = Base()\n"
                                  L"right = Derived()\n");
    Value run_result =
        test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_FALSE(run_result.is_exception_marker());

    Value left = load_global_from_module_for_test(code_obj, left_name);
    Value right = load_global_from_module_for_test(code_obj, right_name);

    OperatorWalkDescriptor descriptor = walk_operator_table(
        test_context.thread(), OperatorDispatchTableId::Add, 0,
        OperatorCacheability::Uncacheable, left, right, Value::not_present());

    ASSERT_EQ(OperatorWalkStatus::CallUntrustedFunction, descriptor.status);
    EXPECT_EQ(OperatorStepAction::CallBinaryReflected, descriptor.action);
    EXPECT_EQ(1u, descriptor.resume_index);
    ASSERT_NE(nullptr, descriptor.cache_entry.function);
    EXPECT_TRUE(descriptor.cache_entry.reflected_untrusted_call);
}

TEST(Interpreter, operator_dispatch_tables_include_unary_and_binary_arithmetic)
{
    test::VmTestContext test_context;

    auto expect_binary_table = [&](OperatorDispatchTableId table_id) {
        const OperatorDispatchTable &table =
            test_context.vm().operator_dispatch_table(table_id);
        ASSERT_EQ(6, table.n_steps);
        EXPECT_EQ(OperatorStepAction::CallBinaryReflected,
                  table.step(0).action);
        EXPECT_NE(nullptr, table.step(0).dunder_name);
        EXPECT_EQ(OperatorStepApplicability::IfArithmeticReflectedPriority,
                  table.step(0).applicability);
        EXPECT_EQ(2, table.step(0).else_skip);
        EXPECT_EQ(OperatorStepAction::CallBinary, table.step(1).action);
        EXPECT_NE(nullptr, table.step(1).dunder_name);
        EXPECT_EQ(OperatorStepAction::RaiseUnsupported, table.step(2).action);
        EXPECT_EQ(OperatorStepAction::CallBinary, table.step(3).action);
        EXPECT_NE(nullptr, table.step(3).dunder_name);
        EXPECT_EQ(OperatorStepAction::CallBinaryReflected,
                  table.step(4).action);
        EXPECT_NE(nullptr, table.step(4).dunder_name);
        EXPECT_EQ(
            OperatorStepApplicability::IfMethodFoundAndOperands01TypesDiffer,
            table.step(4).applicability);
        EXPECT_EQ(OperatorStepAction::RaiseUnsupported, table.step(5).action);
    };

    expect_binary_table(OperatorDispatchTableId::Add);
    expect_binary_table(OperatorDispatchTableId::Sub);
    expect_binary_table(OperatorDispatchTableId::Mul);
    expect_binary_table(OperatorDispatchTableId::MatMul);
    expect_binary_table(OperatorDispatchTableId::BinaryPow);
    expect_binary_table(OperatorDispatchTableId::TrueDiv);
    expect_binary_table(OperatorDispatchTableId::FloorDiv);
    expect_binary_table(OperatorDispatchTableId::Mod);
    expect_binary_table(OperatorDispatchTableId::LShift);
    expect_binary_table(OperatorDispatchTableId::RShift);
    expect_binary_table(OperatorDispatchTableId::And);
    expect_binary_table(OperatorDispatchTableId::Xor);
    expect_binary_table(OperatorDispatchTableId::Or);

    {
        const OperatorDispatchTable &table =
            test_context.vm().operator_dispatch_table(
                OperatorDispatchTableId::TernaryPow);
        ASSERT_EQ(6, table.n_steps);
        EXPECT_EQ(OperatorStepAction::CallTernaryReflected,
                  table.step(0).action);
        EXPECT_NE(nullptr, table.step(0).dunder_name);
        EXPECT_EQ(OperatorStepApplicability::IfArithmeticReflectedPriority,
                  table.step(0).applicability);
        EXPECT_EQ(2, table.step(0).else_skip);
        EXPECT_EQ(OperatorStepAction::CallTernary, table.step(1).action);
        EXPECT_NE(nullptr, table.step(1).dunder_name);
        EXPECT_EQ(OperatorStepAction::RaiseUnsupported, table.step(2).action);
        EXPECT_EQ(OperatorStepAction::CallTernary, table.step(3).action);
        EXPECT_NE(nullptr, table.step(3).dunder_name);
        EXPECT_EQ(OperatorStepAction::CallTernaryReflected,
                  table.step(4).action);
        EXPECT_NE(nullptr, table.step(4).dunder_name);
        EXPECT_EQ(
            OperatorStepApplicability::IfMethodFoundAndOperands01TypesDiffer,
            table.step(4).applicability);
        EXPECT_EQ(OperatorStepAction::RaiseUnsupported, table.step(5).action);
    }

    auto expect_unary_table = [&](OperatorDispatchTableId table_id) {
        const OperatorDispatchTable &table =
            test_context.vm().operator_dispatch_table(table_id);
        ASSERT_EQ(2, table.n_steps);
        EXPECT_EQ(OperatorStepAction::CallUnary, table.step(0).action);
        EXPECT_NE(nullptr, table.step(0).dunder_name);
        EXPECT_EQ(OperatorStepApplicability::IfMethodFound,
                  table.step(0).applicability);
        EXPECT_EQ(OperatorStepAction::RaiseUnsupported, table.step(1).action);
    };

    expect_unary_table(OperatorDispatchTableId::Neg);
    expect_unary_table(OperatorDispatchTableId::Pos);
    expect_unary_table(OperatorDispatchTableId::Invert);

    auto expect_receiver_binary_table = [&](OperatorDispatchTableId table_id) {
        const OperatorDispatchTable &table =
            test_context.vm().operator_dispatch_table(table_id);
        ASSERT_EQ(2, table.n_steps);
        EXPECT_EQ(OperatorStepAction::CallBinary, table.step(0).action);
        EXPECT_NE(nullptr, table.step(0).dunder_name);
        EXPECT_EQ(OperatorStepApplicability::IfMethodFound,
                  table.step(0).applicability);
        EXPECT_EQ(OperatorStepAction::RaiseUnsupported, table.step(1).action);
    };

    expect_receiver_binary_table(OperatorDispatchTableId::GetItem);
    expect_receiver_binary_table(OperatorDispatchTableId::DelItem);

    {
        const OperatorDispatchTable &table =
            test_context.vm().operator_dispatch_table(
                OperatorDispatchTableId::SetItem);
        ASSERT_EQ(2, table.n_steps);
        EXPECT_EQ(OperatorStepAction::CallTernary, table.step(0).action);
        EXPECT_NE(nullptr, table.step(0).dunder_name);
        EXPECT_EQ(OperatorStepApplicability::IfMethodFound,
                  table.step(0).applicability);
        EXPECT_EQ(OperatorStepAction::RaiseUnsupported, table.step(1).action);
    }
}

TEST(Interpreter, operator_add_walk_uses_reflected_fallback_for_different_types)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> left_name(
        test_context.vm().get_or_create_interned_string_value(L"left"));
    TValue<String> right_name(
        test_context.vm().get_or_create_interned_string_value(L"right"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Left:\n"
                                  L"    def __add__(self, other):\n"
                                  L"        return NotImplemented\n"
                                  L"class Right:\n"
                                  L"    def __radd__(self, other):\n"
                                  L"        return 7\n"
                                  L"left = Left()\n"
                                  L"right = Right()\n");
    Value run_result =
        test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_FALSE(run_result.is_exception_marker());

    Value left = load_global_from_module_for_test(code_obj, left_name);
    Value right = load_global_from_module_for_test(code_obj, right_name);

    OperatorWalkDescriptor descriptor = walk_operator_table(
        test_context.thread(), OperatorDispatchTableId::Add, 4,
        OperatorCacheability::Uncacheable, left, right, Value::not_present());

    ASSERT_EQ(OperatorWalkStatus::CallUntrustedFunction, descriptor.status);
    EXPECT_EQ(OperatorStepAction::CallBinaryReflected, descriptor.action);
    EXPECT_EQ(5u, descriptor.resume_index);
    ASSERT_NE(nullptr, descriptor.cache_entry.function);
    EXPECT_TRUE(descriptor.cache_entry.reflected_untrusted_call);
}

TEST(Interpreter, operator_matmul_unsupported_operands_raise)
{
    expect_python_error(L"1 @ 2\n", L"TypeError",
                        L"unsupported operand type(s) for @");
}

TEST(Interpreter, operator_add_dispatch_python_cache_hit)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"add"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Addable:\n"
                                  L"    def __init__(self, value):\n"
                                  L"        self.value = value\n"
                                  L"    def __add__(self, other):\n"
                                  L"        return self.value + other\n"
                                  L"def add(left, right):\n"
                                  L"    return left + right\n"
                                  L"left = Addable(10)\n"
                                  L"add(left, 1)\n"
                                  L"add(left, 2)\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(12), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    EXPECT_FALSE(cache.reflected_untrusted_call);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_add_dispatch_reflected_subclass_python_cache_hit)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"add"));
    CodeObject *code_obj = test_context.compile_file(
        L"class Base:\n"
        L"    def __add__(self, other):\n"
        L"        return 3\n"
        L"class Derived(Base):\n"
        L"    def __init__(self):\n"
        L"        self.marker = 7\n"
        L"    def __radd__(self, other):\n"
        L"        return self.marker\n"
        L"def add(left, right):\n"
        L"    return left + right\n"
        L"left = Base()\n"
        L"right = Derived()\n"
        L"add(left, right) * 10 + add(left, right)\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(77), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    EXPECT_TRUE(cache.reflected_untrusted_call);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_add_dispatch_trusted_str_handler_cache_hit)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"add"));
    CodeObject *code_obj =
        test_context.compile_file(L"def add(left, right):\n"
                                  L"    return left + right\n"
                                  L"add('a', 'b')\n"
                                  L"add('c', 'd')\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(L"cd",
                 string_as_wchar_t(TValue<String>::from_value_assumed(actual)));

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    EXPECT_FALSE(cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_add_dispatch_reflected_float_trusted_cache_hit)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"add"));
    CodeObject *code_obj =
        test_context.compile_file(L"def add(left, right):\n"
                                  L"    return left + right\n"
                                  L"add(1, 1.5)\n"
                                  L"add(2, 1.5)\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(can_convert_to<Float>(actual));
    EXPECT_DOUBLE_EQ(3.5, actual.get_ptr<Float>()->value());

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    EXPECT_FALSE(cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_dispatch_trusted_bigint_arithmetic_cache_hit)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj =
        test_context.compile_file(L"big = int('288230376151711744')\n"
                                  L"def add(left, right):\n"
                                  L"    return left + right\n"
                                  L"def sub(left, right):\n"
                                  L"    return left - right\n"
                                  L"def mul(left, right):\n"
                                  L"    return left * right\n"
                                  L"add(big, 1)\n"
                                  L"add(big, 2)\n"
                                  L"sub(big, 1)\n"
                                  L"sub(big, 2)\n"
                                  L"mul(big, -1)\n"
                                  L"str(mul(big, -2))\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    expect_string_value(actual, L"-576460752303423488");

    auto expect_trusted_cache = [&](const wchar_t *function_name) {
        TValue<String> name(
            test_context.vm().get_or_create_interned_string_value(
                function_name));
        Value function_value = load_global_from_module_for_test(code_obj, name);
        ASSERT_TRUE(can_convert_to<Function>(function_value));
        CodeObject *function_code =
            assume_convert_to<Function>(function_value)->code_object.extract();
        ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
        const OperatorInlineCache &cache =
            function_code->inline_caches.operator_caches[0];
        EXPECT_FALSE(cache.trusted_handler.is_null());
        EXPECT_EQ(nullptr, cache.function);
        ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
        ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
    };

    expect_trusted_cache(L"add");
    expect_trusted_cache(L"sub");
    expect_trusted_cache(L"mul");
}

TEST(Interpreter, operator_eq_dispatch_reflected_python_cache_hit)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"eq"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Base:\n"
                                  L"    def __eq__(self, other):\n"
                                  L"        return 'base'\n"
                                  L"class Derived(Base):\n"
                                  L"    def __init__(self):\n"
                                  L"        self.marker = 6\n"
                                  L"    def __eq__(self, other):\n"
                                  L"        return self.marker\n"
                                  L"def eq(left, right):\n"
                                  L"    return left == right\n"
                                  L"left = Base()\n"
                                  L"right = Derived()\n"
                                  L"eq(left, right) * 10 + eq(left, right)\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(66), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    EXPECT_TRUE(cache.reflected_untrusted_call);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_eq_dispatch_trusted_handler_cache)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"eq"));
    CodeObject *code_obj =
        test_context.compile_file(L"def eq(left, right):\n"
                                  L"    return left == right\n"
                                  L"eq('a', 'a')\n"
                                  L"eq('b', 'b')\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::True(), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    EXPECT_FALSE(cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_eq_dispatch_found_non_callable_raises_call_error)
{
    expect_python_error(L"class EqResult:\n"
                        L"    __eq__ = 123\n"
                        L"EqResult() == EqResult()\n",
                        L"TypeError", L"object is not callable");
}

TEST(Interpreter, DISABLED_operator_eq_dispatch_descriptor_lookup_exceptions)
{
    expect_python_error(L"class RaisesFromLookup:\n"
                        L"    class Descriptor:\n"
                        L"        def __get__(self, obj, cls):\n"
                        L"            raise ValueError\n"
                        L"    __eq__ = Descriptor()\n"
                        L"RaisesFromLookup() == RaisesFromLookup()\n",
                        L"ValueError", L"");
}

TEST(Interpreter, operator_eq_dispatch_call_exceptions_propagate)
{
    expect_python_error(L"class RaisesFromCall:\n"
                        L"    def __eq__(self, other):\n"
                        L"        raise ValueError\n"
                        L"RaisesFromCall() == RaisesFromCall()\n",
                        L"ValueError", L"");
}

TEST(Interpreter, operator_eq_dispatch_python_cache_installs_before_call)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"eq"));
    CodeObject *code_obj =
        test_context.compile_file(L"class EqResult:\n"
                                  L"    fail = True\n"
                                  L"    def __eq__(self, other):\n"
                                  L"        if EqResult.fail:\n"
                                  L"            raise ValueError\n"
                                  L"        return 9\n"
                                  L"def eq(left, right):\n"
                                  L"    return left == right\n"
                                  L"left = EqResult()\n"
                                  L"right = EqResult()\n"
                                  L"caught = 0\n"
                                  L"try:\n"
                                  L"    eq(left, right)\n"
                                  L"except ValueError:\n"
                                  L"    caught = 1\n"
                                  L"caught\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(1), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, sqrt_is_not_a_builtin)
{
    expect_python_error(L"sqrt(4)\n", L"NameError",
                        L"name 'sqrt' is not defined");
}

TEST(Interpreter, string_comparison_reports_unsupported_operands)
{
    expect_python_error(L"\"a\" < 1\n", L"TypeError",
                        L"unsupported operand type(s) for comparison");
}

TEST(Interpreter, subscript_load_passes_binary_slice_object)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        return key\n"
                                 L"Bag()[1:2]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(can_convert_to<Slice>(actual));
    Slice *slice = assume_convert_to<Slice>(actual);
    EXPECT_EQ(Value::from_smi(1), slice->start);
    EXPECT_EQ(Value::from_smi(2), slice->stop);
    EXPECT_EQ(Value::None(), slice->step);
    EXPECT_EQ(file_runner.test_context().vm().slice_step_none_shape(),
              slice->get_shape());
}

TEST(Interpreter, subscript_load_passes_ternary_slice_object)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        return key\n"
                                 L"Bag()[1:2:3]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(can_convert_to<Slice>(actual));
    Slice *slice = assume_convert_to<Slice>(actual);
    EXPECT_EQ(Value::from_smi(1), slice->start);
    EXPECT_EQ(Value::from_smi(2), slice->stop);
    EXPECT_EQ(Value::from_smi(3), slice->step);
    EXPECT_EQ(file_runner.test_context().vm().slice_general_shape(),
              slice->get_shape());
}

TEST(Interpreter, subscript_load_passes_none_for_omitted_slice_bounds)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        return key\n"
                                 L"Bag()[:]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(can_convert_to<Slice>(actual));
    Slice *slice = assume_convert_to<Slice>(actual);
    EXPECT_EQ(Value::None(), slice->start);
    EXPECT_EQ(Value::None(), slice->stop);
    EXPECT_EQ(Value::None(), slice->step);
    EXPECT_EQ(file_runner.test_context().vm().slice_step_none_shape(),
              slice->get_shape());
}

TEST(Interpreter, slice_constructor_uses_python_argument_convention)
{
    test::FileRunner unary_runner(L"slice(5)\n");
    Value unary = unary_runner.return_value;
    ASSERT_TRUE(can_convert_to<Slice>(unary));
    Slice *unary_slice = assume_convert_to<Slice>(unary);
    EXPECT_EQ(Value::None(), unary_slice->start);
    EXPECT_EQ(Value::from_smi(5), unary_slice->stop);
    EXPECT_EQ(Value::None(), unary_slice->step);
    EXPECT_EQ(unary_runner.test_context().vm().slice_step_none_shape(),
              unary_slice->get_shape());

    test::FileRunner ternary_runner(L"slice(1, 2, 3)\n");
    Value ternary = ternary_runner.return_value;
    ASSERT_TRUE(can_convert_to<Slice>(ternary));
    Slice *ternary_slice = assume_convert_to<Slice>(ternary);
    EXPECT_EQ(Value::from_smi(1), ternary_slice->start);
    EXPECT_EQ(Value::from_smi(2), ternary_slice->stop);
    EXPECT_EQ(Value::from_smi(3), ternary_slice->step);
    EXPECT_EQ(ternary_runner.test_context().vm().slice_general_shape(),
              ternary_slice->get_shape());
}

TEST(Interpreter, slice_constructor_rejects_keywords)
{
    expect_python_error(L"slice(stop=5)\n", L"TypeError",
                        L"invalid keyword argument");
}

TEST(Interpreter, slice_indices_rejects_invalid_consumed_values)
{
    expect_python_error(L"slice(None, None, 0).indices(5)\n", L"ValueError",
                        L"slice step cannot be zero");
    expect_python_error(
        L"slice('a', None).indices(5)\n", L"TypeError",
        L"slice indices must be integers or None or have an __index__ method");
    expect_python_error(
        L"slice(None, None, 'x').indices(5)\n", L"TypeError",
        L"slice indices must be integers or None or have an __index__ method");
    expect_python_error(L"slice(None).indices(-1)\n", L"ValueError",
                        L"length should not be negative");
    expect_python_error(
        L"slice(None).indices('x')\n", L"TypeError",
        L"slice indices must be integers or None or have an __index__ method");
}

TEST(Interpreter, normalize_slice_helpers_compute_selected_length)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<Slice> forward =
        make_slice(test_context.thread(), Value::from_smi(1),
                   Value::from_smi(8), Value::None());
    Expected<NormalizedNonstridedSlice> forward_result =
        normalize_nonstrided_slice_for_length(test_context.thread(), forward,
                                              10);
    ASSERT_TRUE(forward_result.has_value());
    NormalizedNonstridedSlice forward_normalized = forward_result.value();
    EXPECT_EQ(1, forward_normalized.start);
    EXPECT_EQ(7u, forward_normalized.selected_sequence_length);

    TValue<Slice> reverse = make_slice(test_context.thread(), Value::None(),
                                       Value::None(), Value::from_smi(-1));
    Expected<NormalizedGeneralSlice> reverse_result =
        normalize_general_slice_for_length(test_context.thread(), reverse, 5);
    ASSERT_TRUE(reverse_result.has_value());
    NormalizedGeneralSlice reverse_normalized = reverse_result.value();
    EXPECT_EQ(4, reverse_normalized.start);
    EXPECT_EQ(-1, reverse_normalized.stop);
    EXPECT_EQ(-1, reverse_normalized.step);
    EXPECT_EQ(5u, reverse_normalized.selected_sequence_length);
}

TEST(Interpreter, subscript_load_caches_inline_key_shape)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"get"));
    CodeObject *code_obj = test_context.compile_file(L"def get(xs, key):\n"
                                                     L"    return xs[key]\n"
                                                     L"xs = [11, 13, 17]\n"
                                                     L"first = get(xs, 1)\n"
                                                     L"second = get(xs, 2)\n"
                                                     L"first * 100 + second\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(1317), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(nullptr, cache.operand_lookup_validity_cells[1]);
    EXPECT_EQ(ShapeKey::from_value(Value::from_smi(1)),
              cache.operand_shape_keys[1]);
    EXPECT_EQ(test_context.vm().smi_shape(),
              test_context.vm().shape_for_key(cache.operand_shape_keys[1]));
    EXPECT_FALSE(cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, cache.function);
}

TEST(Interpreter, subscript_load_caches_getitem_with_default_arguments)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"get"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Bag:\n"
                                  L"    def __getitem__(self, key, extra=5):\n"
                                  L"        return key + extra\n"
                                  L"def get(xs, key):\n"
                                  L"    return xs[key]\n"
                                  L"bag = Bag()\n"
                                  L"first = get(bag, 10)\n"
                                  L"second = get(bag, 20)\n"
                                  L"first * 100 + second\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(1525), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    EXPECT_EQ(2u, cache.n_args);
    EXPECT_TRUE(cache.has_self);
    EXPECT_EQ(FunctionCallAdaptation::Defaultable, cache.adaptation);
}

TEST(Interpreter, subscript_load_replaces_cache_for_different_key_shape)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"get"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Bag:\n"
                                  L"    def __getitem__(self, key):\n"
                                  L"        return 7\n"
                                  L"def get(obj, key):\n"
                                  L"    return obj[key]\n"
                                  L"bag = Bag()\n"
                                  L"first = get(bag, 1)\n"
                                  L"second = get(bag, None)\n"
                                  L"first * 10 + second\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(77), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(nullptr, cache.operand_lookup_validity_cells[1]);

    EXPECT_EQ(ShapeKey::from_value(Value::None()), cache.operand_shape_keys[1]);
    EXPECT_EQ(test_context.thread()->shape_of_value(Value::None()),
              test_context.vm().shape_for_key(cache.operand_shape_keys[1]));
    ASSERT_NE(nullptr, cache.function);
    EXPECT_EQ(2u, cache.n_args);
    EXPECT_TRUE(cache.has_self);
    EXPECT_EQ(FunctionCallAdaptation::FixedArity, cache.adaptation);
}

TEST(Interpreter, subscript_load_caches_slice_key_shape)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"get"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Bag:\n"
                                  L"    def __getitem__(self, key):\n"
                                  L"        return 7\n"
                                  L"def get(obj, stop):\n"
                                  L"    return obj[:stop]\n"
                                  L"bag = Bag()\n"
                                  L"first = get(bag, 1)\n"
                                  L"second = get(bag, 2)\n"
                                  L"first * 10 + second\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(77), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    EXPECT_EQ(ShapeKey::from_shape(test_context.vm().slice_step_none_shape()),
              cache.operand_shape_keys[1]);
    EXPECT_EQ(test_context.vm().slice_step_none_shape(),
              test_context.vm().shape_for_key(cache.operand_shape_keys[1]));
}

TEST(Interpreter, subscript_load_replaces_cache_for_different_slice_shapes)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"get"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Bag:\n"
                                  L"    def __getitem__(self, key):\n"
                                  L"        return 7\n"
                                  L"def get(obj, use_step):\n"
                                  L"    if use_step:\n"
                                  L"        return obj[1:2:3]\n"
                                  L"    return obj[1:2]\n"
                                  L"bag = Bag()\n"
                                  L"first = get(bag, False)\n"
                                  L"second = get(bag, True)\n"
                                  L"first * 10 + second\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(77), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(2u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &general_cache =
        function_code->inline_caches.operator_caches[0];
    const OperatorInlineCache &none_cache =
        function_code->inline_caches.operator_caches[1];
    EXPECT_EQ(ShapeKey::from_shape(test_context.vm().slice_general_shape()),
              general_cache.operand_shape_keys[1]);
    EXPECT_EQ(ShapeKey::from_shape(test_context.vm().slice_step_none_shape()),
              none_cache.operand_shape_keys[1]);
}

TEST(Interpreter, subscript_load_caches_trusted_builtin_slice_handlers)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(
        L"def list_nonstrided(xs):\n"
        L"    return xs[1:3]\n"
        L"def list_general(xs):\n"
        L"    return xs[::-1]\n"
        L"def tuple_nonstrided(xs):\n"
        L"    return xs[1:3]\n"
        L"def tuple_general(xs):\n"
        L"    return xs[::-1]\n"
        L"def str_nonstrided(xs):\n"
        L"    return xs[1:3]\n"
        L"def str_general(xs):\n"
        L"    return xs[::-1]\n"
        L"list_nonstrided_result = list_nonstrided([1, 2, 3, 4])\n"
        L"list_general_result = list_general([1, 2, 3, 4])\n"
        L"tuple_nonstrided_result = tuple_nonstrided((1, 2, 3, 4))\n"
        L"tuple_general_result = tuple_general((1, 2, 3, 4))\n"
        L"str_nonstrided_result = str_nonstrided('abcd')\n"
        L"str_general_result = str_general('abcd')\n"
        L"result = list_nonstrided_result[0] * 10000000\n"
        L"result += list_general_result[0] * 1000000\n"
        L"result += tuple_nonstrided_result[0] * 100000\n"
        L"result += tuple_general_result[0] * 10000\n"
        L"result += list_nonstrided_result[1] * 1000\n"
        L"result += list_general_result[3] * 100\n"
        L"result += tuple_nonstrided_result[1] * 10\n"
        L"result += tuple_general_result[3]\n"
        L"result\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_EQ(Value::from_smi(24243131), actual);

    TValue<String> str_nonstrided_result_name(
        test_context.vm().get_or_create_interned_string_value(
            L"str_nonstrided_result"));
    expect_string_value(
        load_global_from_module_for_test(code_obj, str_nonstrided_result_name),
        L"bc");
    TValue<String> str_general_result_name(
        test_context.vm().get_or_create_interned_string_value(
            L"str_general_result"));
    expect_string_value(
        load_global_from_module_for_test(code_obj, str_general_result_name),
        L"dcba");

    auto expect_trusted_slice_cache = [&](const wchar_t *function_name,
                                          Shape *expected_key_shape,
                                          BinaryHandler &handler_out) {
        TValue<String> function_name_value(
            test_context.vm().get_or_create_interned_string_value(
                function_name));
        Value function_value =
            load_global_from_module_for_test(code_obj, function_name_value);
        ASSERT_TRUE(can_convert_to<Function>(function_value));
        CodeObject *function_code =
            assume_convert_to<Function>(function_value)->code_object.extract();
        const OperatorInlineCache *cache = nullptr;
        for(const OperatorInlineCache &candidate:
            function_code->inline_caches.operator_caches)
        {
            if(!candidate.trusted_handler.is_null() &&
               candidate.operand_shape_keys[1] ==
                   ShapeKey::from_shape(expected_key_shape))
            {
                cache = &candidate;
                break;
            }
        }
        ASSERT_NE(nullptr, cache);
        EXPECT_EQ(ShapeKey::from_shape(expected_key_shape),
                  cache->operand_shape_keys[1]);
        EXPECT_EQ(expected_key_shape, test_context.vm().shape_for_key(
                                          cache->operand_shape_keys[1]));
        EXPECT_NE(nullptr, cache->trusted_handler.binary);
        handler_out = cache->trusted_handler.binary;
    };

    BinaryHandler list_nonstrided_handler = nullptr;
    BinaryHandler tuple_nonstrided_handler = nullptr;
    BinaryHandler str_nonstrided_handler = nullptr;
    BinaryHandler list_general_handler = nullptr;
    BinaryHandler tuple_general_handler = nullptr;
    BinaryHandler str_general_handler = nullptr;

    expect_trusted_slice_cache(L"list_nonstrided",
                               test_context.vm().slice_step_none_shape(),
                               list_nonstrided_handler);
    expect_trusted_slice_cache(L"tuple_nonstrided",
                               test_context.vm().slice_step_none_shape(),
                               tuple_nonstrided_handler);
    expect_trusted_slice_cache(L"str_nonstrided",
                               test_context.vm().slice_step_none_shape(),
                               str_nonstrided_handler);
    expect_trusted_slice_cache(L"list_general",
                               test_context.vm().slice_general_shape(),
                               list_general_handler);
    expect_trusted_slice_cache(L"tuple_general",
                               test_context.vm().slice_general_shape(),
                               tuple_general_handler);
    expect_trusted_slice_cache(L"str_general",
                               test_context.vm().slice_general_shape(),
                               str_general_handler);

    EXPECT_NE(list_nonstrided_handler, list_general_handler);
    EXPECT_NE(tuple_nonstrided_handler, tuple_general_handler);
    EXPECT_NE(str_nonstrided_handler, str_general_handler);
}

TEST(Interpreter, subscript_store_replaces_cache_for_different_key_shape)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"set_item"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Bag:\n"
                                  L"    def __init__(self):\n"
                                  L"        self.total = 0\n"
                                  L"    def __setitem__(self, key, value):\n"
                                  L"        self.total += value\n"
                                  L"def set_item(obj, key, value):\n"
                                  L"    obj[key] = value\n"
                                  L"bag = Bag()\n"
                                  L"set_item(bag, 1, 7)\n"
                                  L"set_item(bag, None, 11)\n"
                                  L"bag.total\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(18), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(nullptr, cache.operand_lookup_validity_cells[1]);
    EXPECT_EQ(ShapeKey::from_value(Value::None()), cache.operand_shape_keys[1]);
    ASSERT_NE(nullptr, cache.function);
    EXPECT_EQ(3u, cache.n_args);
    EXPECT_TRUE(cache.has_self);
    EXPECT_EQ(FunctionCallAdaptation::FixedArity, cache.adaptation);
}

TEST(Interpreter, subscript_store_cache_ignores_value_shape)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"set_item"));
    CodeObject *code_obj =
        test_context.compile_file(L"class Bag:\n"
                                  L"    def __init__(self):\n"
                                  L"        self.total = 0\n"
                                  L"    def __setitem__(self, key, value):\n"
                                  L"        if value is None:\n"
                                  L"            self.total += key\n"
                                  L"        else:\n"
                                  L"            self.total += value\n"
                                  L"def set_item(obj, key, value):\n"
                                  L"    obj[key] = value\n"
                                  L"bag = Bag()\n"
                                  L"set_item(bag, 1, 7)\n"
                                  L"set_item(bag, 2, None)\n"
                                  L"bag.total\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(9), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->inline_caches.operator_caches.size());
    const OperatorInlineCache &cache =
        function_code->inline_caches.operator_caches[0];
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(ShapeKey::from_value(Value::from_smi(0)),
              cache.operand_shape_keys[1]);
    ASSERT_NE(nullptr, cache.function);
    EXPECT_EQ(3u, cache.n_args);
}

TEST(Interpreter, dict_literal_returns_dict_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    Value actual = test_context.run_file(L"key = \"alpha\"\n"
                                         L"{key: 7, \"beta\": 9}\n");

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Dict,
              actual.get_ptr<Object>()->native_layout_id());
    Dict *dict = actual.get_ptr<Dict>();
    EXPECT_EQ(2u, dict->size());

    Value alpha = test_context.vm()
                      .get_or_create_interned_string_value(L"alpha")
                      .raw_value();
    Value beta = test_context.vm()
                     .get_or_create_interned_string_value(L"beta")
                     .raw_value();
    EXPECT_EQ(
        Value::from_smi(7),
        dict->get_item_for_str(test_context.thread(),
                               TValue<String>::from_value_unchecked(alpha))
            .value());
    EXPECT_EQ(Value::from_smi(9),
              dict->get_item_for_str(test_context.thread(),
                                     TValue<String>::from_value_unchecked(beta))
                  .value());
}

TEST(Interpreter,
     dict_subscript_augmented_assignment_evaluates_receiver_and_key_once)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"xs = {\"alpha\": 10, \"beta\": 20}\n"
                                  L"def get_dict():\n"
                                  L"    return xs\n"
                                  L"def next_key():\n"
                                  L"    next_counter()\n"
                                  L"    return \"alpha\"\n"
                                  L"get_dict()[next_key()] += 7\n"
                                  L"xs[\"alpha\"]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_intrinsic_function(&test_context.vm(), native_next_counter)
            .value()
            .raw_value();
    ASSERT_TRUE(store_module_global(code_obj->get_defining_module().extract(),
                                    name, next_counter));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_store_rejects_tuple_item_assignment)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls.__mro__[0] = 1\n",
                        L"TypeError",
                        L"'tuple' object does not support item assignment");
}

TEST(Interpreter, subscript_store_tuple_item_assignment_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    class Cls:\n"
                        L"        pass\n"
                        L"    Cls.__mro__[0] = 1\n"
                        L"fail()\n",
                        L"TypeError",
                        L"'tuple' object does not support item assignment");
}

TEST(Interpreter, subscript_delete_rejects_tuple_item_deletion)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"del Cls.__mro__[0]\n",
                        L"TypeError",
                        L"'tuple' object does not support item deletion");
}

TEST(Interpreter, subscript_delete_tuple_item_deletion_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    class Cls:\n"
                        L"        pass\n"
                        L"    del Cls.__mro__[0]\n"
                        L"fail()\n",
                        L"TypeError",
                        L"'tuple' object does not support item deletion");
}

TEST(Interpreter,
     subscript_augmented_assignment_evaluates_receiver_and_index_once)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"xs = [10, 20, 30]\n"
                                  L"def get_list():\n"
                                  L"    return xs\n"
                                  L"get_list()[next_counter()] += 7\n"
                                  L"xs[0]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_intrinsic_function(&test_context.vm(), native_next_counter)
            .value()
            .raw_value();
    ASSERT_TRUE(store_module_global(code_obj->get_defining_module().extract(),
                                    name, next_counter));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_load_rejects_non_integer_list_index)
{
    expect_python_error(L"xs = [1, 2, 3]\n"
                        L"xs['x']\n",
                        L"TypeError",
                        L"list indices must be integers or slices");
}

TEST(Interpreter, subscript_load_non_integer_list_index_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = [1, 2, 3]\n"
                        L"    return xs['x']\n"
                        L"fail()\n",
                        L"TypeError",
                        L"list indices must be integers or slices");
}

TEST(Interpreter, subscript_load_rejects_non_integer_tuple_index)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls.__mro__['x']\n",
                        L"TypeError",
                        L"tuple indices must be integers or slices");
}

TEST(Interpreter, subscript_load_slice_reports_invalid_consumed_values)
{
    expect_python_error(L"[1, 2, 3][slice(None, None, 0)]\n", L"ValueError",
                        L"slice step cannot be zero");
    expect_python_error(
        L"(1, 2, 3)[slice('a', None)]\n", L"TypeError",
        L"slice indices must be integers or None or have an __index__ method");
    expect_python_error(
        L"'abc'[slice(None, None, 'x')]\n", L"TypeError",
        L"slice indices must be integers or None or have an __index__ method");
}

TEST(Interpreter, subscript_load_rejects_non_integer_string_index)
{
    expect_python_error(L"'abc'['x']\n", L"TypeError",
                        L"string indices must be integers or slices");
}

TEST(Interpreter, subscript_load_rejects_oversized_bigint_index)
{
    expect_python_error(L"[1, 2][int('288230376151711744')]\n",
                        L"OverflowError", L"integer overflow");
    expect_python_error(L"(1, 2)[int('288230376151711744')]\n",
                        L"OverflowError", L"integer overflow");
    expect_python_error(L"'ab'[int('288230376151711744')]\n", L"OverflowError",
                        L"integer overflow");
    expect_python_error(L"[1, 2][slice(int('288230376151711744'), None)]\n",
                        L"OverflowError", L"integer overflow");
}

TEST(Interpreter, subscript_load_rejects_out_of_range_list_index)
{
    expect_python_error(L"xs = [1, 2, 3]\n"
                        L"xs[3]\n",
                        L"IndexError", L"list index out of range");
}

TEST(Interpreter, subscript_load_out_of_range_list_index_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = [1, 2, 3]\n"
                        L"    return xs[3]\n"
                        L"fail()\n",
                        L"IndexError", L"list index out of range");
}

TEST(Interpreter, subscript_load_rejects_out_of_range_tuple_index)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls.__mro__[2]\n",
                        L"IndexError", L"tuple index out of range");
}

TEST(Interpreter, subscript_load_out_of_range_tuple_index_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    class Cls:\n"
                        L"        pass\n"
                        L"    return Cls.__mro__[2]\n"
                        L"fail()\n",
                        L"IndexError", L"tuple index out of range");
}

TEST(Interpreter, subscript_load_missing_dict_key_raises_key_error)
{
    expect_python_error(L"xs = {\"alpha\": 1}\n"
                        L"xs[\"beta\"]\n",
                        L"KeyError", L"");
}

TEST(Interpreter, subscript_load_missing_dict_key_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = {\"alpha\": 1}\n"
                        L"    return xs[\"beta\"]\n"
                        L"fail()\n",
                        L"KeyError", L"");
}

TEST(Interpreter, subscript_load_rejects_non_subscriptable_receiver)
{
    expect_python_error(L"1[0]\n", L"TypeError",
                        L"object is not subscriptable");
}

TEST(Interpreter, subscript_load_non_subscriptable_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    return 1[0]\n"
                        L"fail()\n",
                        L"TypeError", L"object is not subscriptable");
}

TEST(Interpreter, subscript_delete_rejects_non_subscriptable_receiver)
{
    expect_python_error(L"del (1)[0]\n", L"TypeError",
                        L"object is not subscriptable");
}

TEST(Interpreter, subscript_delete_missing_dict_key_raises_key_error)
{
    expect_python_error(L"xs = {\"alpha\": 1}\n"
                        L"del xs[\"beta\"]\n",
                        L"KeyError", L"");
}

TEST(Interpreter, subscript_delete_missing_dict_key_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = {\"alpha\": 1}\n"
                        L"    del xs[\"beta\"]\n"
                        L"fail()\n",
                        L"KeyError", L"");
}

TEST(Interpreter, attribute_load_and_store_syntax)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> obj_name(
        test_context.vm().get_or_create_interned_string_value(L"obj"));
    TValue<String> attr_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"obj = Cls()\n"
                                                     L"obj.value = 7\n"
                                                     L"obj.value\n");
    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(7), actual);
    Value obj_value = load_global_from_module_for_test(code_obj, obj_name);
    ASSERT_TRUE(obj_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::Instance,
              obj_value.get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(Value::from_smi(7),
              obj_value.get_ptr<Instance>()->get_own_property(attr_name));
}

TEST(Interpreter, store_attr_caches_instance_add_transition)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"make"));
    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"def make(value):\n"
                                                     L"    obj = Cls()\n"
                                                     L"    obj.value = value\n"
                                                     L"    return obj.value\n"
                                                     L"make(1)\n"
                                                     L"make(2)\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(2), actual);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u,
              function_code->inline_caches.attribute_mutation_caches.size());
    const AttributeMutationInlineCache &cache =
        function_code->inline_caches.attribute_mutation_caches[0];
    ASSERT_NE(nullptr, cache.receiver_shape);
    ASSERT_NE(nullptr, cache.plan.next_shape);
    ASSERT_TRUE(cache.plan.storage_location().is_found());
    EXPECT_TRUE(cache.plan.is_add_own_property());
    ASSERT_NE(nullptr, cache.lookup_validity_cell);
    EXPECT_TRUE(cache.lookup_validity_cell->is_valid());
}

TEST(Interpreter, del_attr_deletes_instance_property_and_caches_plan)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> clear_name(
        test_context.vm().get_or_create_interned_string_value(L"clear"));
    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *definition_code =
        test_context.compile_file(L"def clear(obj):\n"
                                  L"    del obj.value\n");
    (void)test_context.thread()->run_clovervm_code_object(definition_code);
    Value function_value =
        load_global_from_module_for_test(definition_code, clear_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(uint8_t(Bytecode::DelAttr), function_code->code[0]);
    ASSERT_TRUE(function_code->inline_caches.attribute_read_caches.empty());
    ASSERT_EQ(1u,
              function_code->inline_caches.attribute_mutation_caches.size());

    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *first = test_context.thread()->make_internal_raw<Instance>(cls);
    ASSERT_TRUE(first->set_own_property(value_name, Value::from_smi(7)));

    CodeObject *call_code = test_context.compile_file(L"clear(obj)\n"
                                                      L"42\n");
    store_global_to_module_for_test(test_context, call_code, L"clear",
                                    function_value);
    store_global_to_module_for_test(test_context, call_code, L"obj",
                                    Value::from_oop(first));
    EXPECT_EQ(Value::from_smi(42),
              test_context.thread()->run_clovervm_code_object(call_code));
    EXPECT_TRUE(first->get_own_property(value_name).is_not_present());

    ASSERT_EQ(1u,
              function_code->inline_caches.attribute_mutation_caches.size());
    const AttributeMutationInlineCache &cache =
        function_code->inline_caches.attribute_mutation_caches[0];
    ASSERT_NE(nullptr, cache.receiver_shape);
    EXPECT_TRUE(cache.plan.is_delete_own_property());
    ASSERT_NE(nullptr, cache.plan.next_shape);
    EXPECT_TRUE(cache.plan.storage_location().is_found());
    ASSERT_NE(nullptr, cache.lookup_validity_cell);
    EXPECT_TRUE(cache.lookup_validity_cell->is_valid());

    Instance *second = test_context.thread()->make_internal_raw<Instance>(cls);
    ASSERT_TRUE(second->set_own_property(value_name, Value::from_smi(8)));
    store_global_to_module_for_test(test_context, call_code, L"obj",
                                    Value::from_oop(second));
    EXPECT_EQ(Value::from_smi(42),
              test_context.thread()->run_clovervm_code_object(call_code));
    EXPECT_TRUE(second->get_own_property(value_name).is_not_present());
}

TEST(Interpreter, del_attr_missing_attribute_raises_attribute_error)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> clear_name(
        test_context.vm().get_or_create_interned_string_value(L"clear"));
    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));

    CodeObject *definition_code =
        test_context.compile_file(L"def clear(obj):\n"
                                  L"    del obj.value\n");
    (void)test_context.thread()->run_clovervm_code_object(definition_code);
    Value function_value =
        load_global_from_module_for_test(definition_code, clear_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));

    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(cls);
    CodeObject *call_code = test_context.compile_file(L"clear(obj)\n");
    store_global_to_module_for_test(test_context, call_code, L"clear",
                                    function_value);
    store_global_to_module_for_test(test_context, call_code, L"obj",
                                    Value::from_oop(obj));

    Value actual = test_context.thread()->run_clovervm_code_object(call_code);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"AttributeError", L"");
}

TEST(Interpreter, cached_class_chain_attribute_read_observes_mro_mutations)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base, NativeLayoutId::Instance);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid, NativeLayoutId::Instance);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    ASSERT_TRUE(base->set_own_property(value_name, Value::from_smi(1)));

    CodeObject *read_code = test_context.compile_file(L"obj.value\n");
    store_global_to_module_for_test(test_context, read_code, L"obj",
                                    Value::from_oop(obj));

    EXPECT_EQ(Value::from_smi(1),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(base->set_own_property(value_name, Value::from_smi(2)));
    EXPECT_EQ(Value::from_smi(2),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(mid->set_own_property(value_name, Value::from_smi(3)));
    EXPECT_EQ(Value::from_smi(3),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(leaf->set_own_property(value_name, Value::from_smi(4)));
    EXPECT_EQ(Value::from_smi(4),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(obj->set_own_property(value_name, Value::from_smi(5)));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(base->set_own_property(value_name, Value::from_smi(6)));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(obj->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(4),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(leaf->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(3),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(mid->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(6),
              test_context.thread()->run_clovervm_code_object(read_code));
}

TEST(Interpreter, cached_direct_method_call_observes_mro_mutations)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base, NativeLayoutId::Instance);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid, NativeLayoutId::Instance);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    Value base_method_1 = make_test_function(test_context, L"base_method_1",
                                             L"def base_method_1(self):\n"
                                             L"    return 1\n");
    Value base_method_2 = make_test_function(test_context, L"base_method_2",
                                             L"def base_method_2(self):\n"
                                             L"    return 2\n");
    Value mid_method = make_test_function(test_context, L"mid_method",
                                          L"def mid_method(self):\n"
                                          L"    return 3\n");
    Value leaf_method = make_test_function(test_context, L"leaf_method",
                                           L"def leaf_method(self):\n"
                                           L"    return 4\n");
    Value own_method = make_test_function(test_context, L"own_method",
                                          L"def own_method():\n"
                                          L"    return 5\n");

    ASSERT_TRUE(base->set_own_property(method_name, base_method_1));

    CodeObject *call_code = test_context.compile_file(L"obj.method()\n");
    store_global_to_module_for_test(test_context, call_code, L"obj",
                                    Value::from_oop(obj));

    EXPECT_EQ(Value::from_smi(1),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(base->set_own_property(method_name, base_method_2));
    EXPECT_EQ(Value::from_smi(2),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(mid->set_own_property(method_name, mid_method));
    EXPECT_EQ(Value::from_smi(3),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(leaf->set_own_property(method_name, leaf_method));
    EXPECT_EQ(Value::from_smi(4),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(obj->set_own_property(method_name, own_method));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(base->set_own_property(method_name, base_method_1));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(obj->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(4),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(leaf->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(3),
              test_context.thread()->run_clovervm_code_object(call_code));

    EXPECT_TRUE(mid->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(1),
              test_context.thread()->run_clovervm_code_object(call_code));
}

TEST(Interpreter, cached_attribute_stores_invalidate_class_chain_reads)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base, NativeLayoutId::Instance);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid, NativeLayoutId::Instance);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    ASSERT_TRUE(base->set_own_property(value_name, Value::from_smi(1)));

    CodeObject *read_code = test_context.compile_file(L"obj.value\n");
    store_global_to_module_for_test(test_context, read_code, L"obj",
                                    Value::from_oop(obj));
    EXPECT_EQ(Value::from_smi(1),
              test_context.thread()->run_clovervm_code_object(read_code));

    CodeObject *base_store_code =
        test_context.compile_file(L"Base.value = new_value\n"
                                  L"obj.value\n");
    store_global_to_module_for_test(test_context, base_store_code, L"Base",
                                    Value::from_oop(base));
    store_global_to_module_for_test(test_context, base_store_code, L"obj",
                                    Value::from_oop(obj));

    store_global_to_module_for_test(test_context, base_store_code, L"new_value",
                                    Value::from_smi(2));
    EXPECT_EQ(Value::from_smi(2),
              test_context.thread()->run_clovervm_code_object(base_store_code));
    EXPECT_EQ(Value::from_smi(2),
              test_context.thread()->run_clovervm_code_object(read_code));

    CodeObject *mid_store_code =
        test_context.compile_file(L"Mid.value = new_value\n"
                                  L"obj.value\n");
    store_global_to_module_for_test(test_context, mid_store_code, L"Mid",
                                    Value::from_oop(mid));
    store_global_to_module_for_test(test_context, mid_store_code, L"obj",
                                    Value::from_oop(obj));

    store_global_to_module_for_test(test_context, mid_store_code, L"new_value",
                                    Value::from_smi(3));
    EXPECT_EQ(Value::from_smi(3),
              test_context.thread()->run_clovervm_code_object(mid_store_code));
    EXPECT_EQ(Value::from_smi(3),
              test_context.thread()->run_clovervm_code_object(read_code));

    CodeObject *leaf_store_code =
        test_context.compile_file(L"Leaf.value = new_value\n"
                                  L"obj.value\n");
    store_global_to_module_for_test(test_context, leaf_store_code, L"Leaf",
                                    Value::from_oop(leaf));
    store_global_to_module_for_test(test_context, leaf_store_code, L"obj",
                                    Value::from_oop(obj));

    store_global_to_module_for_test(test_context, leaf_store_code, L"new_value",
                                    Value::from_smi(4));
    EXPECT_EQ(Value::from_smi(4),
              test_context.thread()->run_clovervm_code_object(leaf_store_code));
    EXPECT_EQ(Value::from_smi(4),
              test_context.thread()->run_clovervm_code_object(read_code));

    CodeObject *self_store_code =
        test_context.compile_file(L"obj.value = new_value\n"
                                  L"obj.value\n");
    store_global_to_module_for_test(test_context, self_store_code, L"obj",
                                    Value::from_oop(obj));

    store_global_to_module_for_test(test_context, self_store_code, L"new_value",
                                    Value::from_smi(5));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(self_store_code));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(read_code));

    store_global_to_module_for_test(test_context, base_store_code, L"new_value",
                                    Value::from_smi(6));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(base_store_code));
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(read_code));

    EXPECT_TRUE(obj->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(4),
              test_context.thread()->run_clovervm_code_object(read_code));
}

TEST(Interpreter, class_definition_rejects_non_class_base)
{
    expect_python_error(L"Base = 1\n"
                        L"class Derived(Base):\n"
                        L"    pass\n",
                        L"TypeError", L"class bases must be class objects");
}

TEST(Interpreter, class_definition_rejects_duplicate_base)
{
    expect_python_error(L"class Base:\n"
                        L"    pass\n"
                        L"class Derived(Base, Base):\n"
                        L"    pass\n",
                        L"TypeError", L"duplicate base class");
}

TEST(Interpreter, class_definition_rejects_inconsistent_c3_mro)
{
    expect_python_error(L"class X:\n"
                        L"    pass\n"
                        L"class Y:\n"
                        L"    pass\n"
                        L"class A(X, Y):\n"
                        L"    pass\n"
                        L"class B(Y, X):\n"
                        L"    pass\n"
                        L"class C(A, B):\n"
                        L"    pass\n",
                        L"TypeError",
                        L"cannot create a consistent method resolution order");
}

TEST(Interpreter, class_call_rejects_arguments)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls(1)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, return_in_class_body_is_rejected)
{
    expect_python_error(L"class Cls:\n"
                        L"    return 1\n",
                        L"SyntaxError", L"'return' outside function");
}

TEST(Interpreter, return_in_module_body_is_rejected)
{
    expect_python_error(L"return\n", L"SyntaxError",
                        L"'return' outside function");
}

TEST(Interpreter, global_statement_makes_function_delete_global)
{
    expect_python_error(L"a = 10\n"
                        L"def f():\n"
                        L"    global a\n"
                        L"    del a\n"
                        L"f()\n"
                        L"a\n",
                        L"NameError", L"name 'a' is not defined");
}

TEST(Interpreter, global_statement_rejects_parameter_conflict)
{
    expect_python_error(L"def f(value):\n"
                        L"    global value\n",
                        L"SyntaxError", L"name is parameter and global");
}

TEST(Interpreter, global_statement_rejects_prior_function_read)
{
    expect_python_error(L"def f():\n"
                        L"    value\n"
                        L"    global value\n",
                        L"SyntaxError",
                        L"name is used prior to global declaration");
}

TEST(Interpreter, global_statement_rejects_prior_function_assignment)
{
    expect_python_error(L"def f():\n"
                        L"    value = 1\n"
                        L"    global value\n",
                        L"SyntaxError",
                        L"name is assigned to before global declaration");
}

TEST(Interpreter, global_statement_rejects_prior_function_delete)
{
    expect_python_error(L"def f():\n"
                        L"    del value\n"
                        L"    global value\n",
                        L"SyntaxError",
                        L"name is assigned to before global declaration");
}

TEST(Interpreter, module_global_statement_rejects_prior_read)
{
    expect_python_error(L"value\n"
                        L"global value\n",
                        L"SyntaxError",
                        L"name is used prior to global declaration");
}

TEST(Interpreter, module_global_statement_rejects_prior_assignment)
{
    expect_python_error(L"value = 1\n"
                        L"global value\n",
                        L"SyntaxError",
                        L"name is assigned to before global declaration");
}

TEST(Interpreter, global_statement_rejects_annotated_name)
{
    expect_python_error(L"def f():\n"
                        L"    global value\n"
                        L"    value: int\n",
                        L"SyntaxError", L"annotated name can't be global");
}

TEST(Interpreter, name_error)
{
    expect_python_error(L"missing_name\n", L"NameError",
                        L"name 'missing_name' is not defined");
}

TEST(Interpreter, call_non_callable)
{
    expect_python_error(L"1()\n", L"TypeError", L"object is not callable");
}

TEST(Interpreter, call_non_callable_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    1()\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError", L"object is not callable");
}

TEST(Interpreter, call_intrinsic_zero_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_zero()\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_zero",
        make_intrinsic_function(&test_context.vm(), native_zero));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
}

TEST(Interpreter, call_intrinsic_seven_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"native_sum7(1, 2, 3, 4, 5, 6, 7)\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_sum7",
        make_intrinsic_function(&test_context.vm(), native_sum7));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(28), actual);
}

TEST(Interpreter, every_safepoint_reclamation_reclaims_unrooted_object)
{
    g_every_safepoint_reclamation_target_address = nullptr;
    g_every_safepoint_reclamation_valid_objects = 0;

    test::VmTestContext test_context;
    ThreadState *thread = test_context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    CodeObject *code_obj =
        test_context.compile_file(L"def exercise():\n"
                                  L"    x = make_large_tuple()\n"
                                  L"    capture_target(x)\n"
                                  L"    x = 1\n"
                                  L"    reclamation_ping()\n"
                                  L"exercise()\n");
    store_global_to_module_for_test(
        test_context, code_obj, L"make_large_tuple",
        make_intrinsic_function(
            &test_context.vm(),
            native_large_tuple_for_every_safepoint_reclamation));
    store_global_to_module_for_test(
        test_context, code_obj, L"capture_target",
        make_intrinsic_function(
            &test_context.vm(),
            native_capture_every_safepoint_reclamation_target));
    store_global_to_module_for_test(
        test_context, code_obj, L"reclamation_ping",
        make_intrinsic_function(&test_context.vm(),
                                native_every_safepoint_reclamation_ping));
    GlobalHeap &heap = test_context.vm().get_refcounted_global_heap();
    test_context.vm().set_fire_every_safepoint_for_testing(true);
    test_context.vm().request_safepoint();

    Value result = thread->run_clovervm_code_object(code_obj);

    ASSERT_FALSE(result.is_exception_marker());
    ASSERT_NE(nullptr, g_every_safepoint_reclamation_target_address);
    EXPECT_LT(heap.count_valid_objects_slow(),
              g_every_safepoint_reclamation_valid_objects);
    EXPECT_FALSE(heap.has_slab_for_address_for_testing(
        g_every_safepoint_reclamation_target_address));
    EXPECT_FALSE(
        thread->zero_count_table_contains_for_testing(static_cast<HeapObject *>(
            g_every_safepoint_reclamation_target_address)));
}

TEST(Interpreter, intrinsic_function_thunk_uses_return_or_raise_adapter)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<Function> native =
        make_intrinsic_function(&test_context.vm(), native_zero).value();

    std::string actual =
        fmt::to_string(*native.extract()->code_object.extract());
    std::string expected = "Code object:\n"
                           "    0 CallIntrinsic0 0\n"
                           "    2 ReturnOrRaiseException\n";
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, call_intrinsic_sets_clover_frame_frontier)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    g_native_frame_frontier_seen = nullptr;
    CodeObject *code_obj = test_context.compile_file(
        L"native_frame0() + native_frame1(10) + "
        L"native_frame2(10, 20) + native_frame3(10, 20, 30)\n");
    store_global_to_module_for_test(
        test_context, code_obj, L"native_frame0",
        make_intrinsic_function(&test_context.vm(), native_frame_frontier0));
    store_global_to_module_for_test(
        test_context, code_obj, L"native_frame1",
        make_intrinsic_function(&test_context.vm(), native_frame_frontier1));
    store_global_to_module_for_test(
        test_context, code_obj, L"native_frame2",
        make_intrinsic_function(&test_context.vm(), native_frame_frontier2));
    store_global_to_module_for_test(
        test_context, code_obj, L"native_frame3",
        make_intrinsic_function(&test_context.vm(), native_frame_frontier3));

    Value *sentinel_fp = test_context.thread()->clover_frame_sentinel();
    EXPECT_NE(nullptr, sentinel_fp);
    EXPECT_EQ(sentinel_fp, test_context.thread()->clover_frame_frontier());
    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(15), actual);
    EXPECT_NE(nullptr, g_native_frame_frontier_seen);
    EXPECT_EQ(sentinel_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_NE(g_native_frame_frontier_seen,
              test_context.thread()->clover_frame_frontier());
}

TEST(Interpreter, thread_state_starts_with_terminated_clover_frame_sentinel)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value *sentinel_fp = test_context.thread()->clover_frame_sentinel();
    EXPECT_EQ(sentinel_fp, test_context.thread()->clover_frame_frontier());
    CodeObject *code_obj = test_context.compile_file(L"42\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(sentinel_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_TRUE(clover_frame_chain_reaches_terminated_root(sentinel_fp,
                                                           sentinel_fp, 1));
}

TEST(Interpreter, clover_frame_frontier_chain_survives_nested_native_reentry)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    g_expected_clover_frame_sentinel =
        test_context.thread()->clover_frame_sentinel();
    EXPECT_EQ(g_expected_clover_frame_sentinel,
              test_context.thread()->clover_frame_frontier());
    g_weave_frontier_checks = 0;

    CodeObject *code_obj =
        test_context.compile_file(L"def inner():\n"
                                  L"    return native_inner()\n"
                                  L"def outer():\n"
                                  L"    return native_outer(inner)\n"
                                  L"outer()\n");
    store_global_to_module_for_test(
        test_context, code_obj, L"native_inner",
        make_intrinsic_function(&test_context.vm(), native_weave_inner));
    store_global_to_module_for_test(
        test_context, code_obj, L"native_outer",
        make_intrinsic_function(&test_context.vm(), native_weave_outer));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(3u, g_weave_frontier_checks);
    EXPECT_EQ(g_expected_clover_frame_sentinel,
              test_context.thread()->clover_frame_frontier());
    g_expected_clover_frame_sentinel = nullptr;
}

TEST(Interpreter, return_to_native_restores_clover_frame_frontier)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_object = make_return_to_native_code(test_context);
    Value *caller_fp = test_context.thread()->clover_frame_frontier();
    Value *wrapper_fp =
        prepare_native_return_wrapper_frame(test_context.thread());

    Value actual =
        run_interpreter(wrapper_fp, code_object, 0, test_context.thread());

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
}

TEST(Interpreter,
     return_exception_marker_to_native_restores_clover_frame_frontier)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_object =
        make_return_exception_marker_to_native_code(test_context);
    Value *caller_fp = test_context.thread()->clover_frame_frontier();
    Value *wrapper_fp =
        prepare_native_return_wrapper_frame(test_context.thread());
    (void)test_context.thread()->set_pending_stop_iteration_no_value();

    Value actual =
        run_interpreter(wrapper_fp, code_object, 0, test_context.thread());

    EXPECT_TRUE(actual.is_exception_marker());
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_TRUE(test_context.thread()->has_pending_exception());
    EXPECT_EQ(PendingExceptionKind::StopIteration,
              test_context.thread()->pending_exception_kind());
    test_context.thread()->clear_pending_exception();
}

TEST(Interpreter, return_exception_marker_to_native_requires_pending_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_object =
        make_return_exception_marker_to_native_code(test_context);
    Value *wrapper_fp =
        prepare_native_return_wrapper_frame(test_context.thread());

    Value actual =
        run_interpreter(wrapper_fp, code_object, 0, test_context.thread());
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(
        test_context.thread(), L"SystemError",
        L"exception marker native return without pending exception");
}

TEST(Interpreter, clover_function_entry_adapter_bytecode_shape)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *adapter =
        test_context.vm().clover_function_entry_adapter(2).value();

    std::string actual = fmt::to_string(*adapter);
    std::string expected = "Code object:\n"
                           "    0 Mov r0, p0\n"
                           "    3 Mov r2, p1\n"
                           "    6 Mov r3, p2\n"
                           "    9 CallPositional r0, {r2..r3}, call_ic[0]\n"
                           "   14 ReturnToNative\n"
                           "   15 ReturnExceptionMarkerToNative\n"
                           "Exception table:\n"
                           "    9..14 -> 15\n";
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, clover_function_entry_adapter_calls_managed_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value function = make_test_function(test_context, L"f",
                                        L"def f(a, b):\n"
                                        L"    return a + b\n");
    CodeObject *adapter =
        test_context.vm().clover_function_entry_adapter(2).value();
    Value args[] = {Value::from_smi(20), Value::from_smi(22)};
    Value *caller_fp = test_context.thread()->clover_frame_frontier();
    Value *adapter_fp = prepare_clover_function_entry_adapter_frame(
        test_context.thread(), adapter, function, args, 2);

    Value actual =
        run_interpreter(adapter_fp, adapter, 0, test_context.thread());

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, clover_function_entry_adapter_returns_pending_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value function = make_test_function(test_context, L"f",
                                        L"def f():\n"
                                        L"    raise ValueError\n");
    CodeObject *adapter =
        test_context.vm().clover_function_entry_adapter(0).value();
    Value *caller_fp = test_context.thread()->clover_frame_frontier();
    Value *adapter_fp = prepare_clover_function_entry_adapter_frame(
        test_context.thread(), adapter, function, nullptr, 0);

    Value actual =
        run_interpreter(adapter_fp, adapter, 0, test_context.thread());

    EXPECT_TRUE(actual.is_exception_marker());
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_TRUE(test_context.thread()->has_pending_exception());
    EXPECT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    test_context.thread()->clear_pending_exception();
}

TEST(Interpreter, call_clovervm_function_overloads_call_managed_functions)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value zero = make_test_function(test_context, L"zero",
                                    L"def zero():\n"
                                    L"    return 7\n");
    Value inc = make_test_function(test_context, L"inc",
                                   L"def inc(a):\n"
                                   L"    return a + 1\n");
    Value add = make_test_function(test_context, L"add",
                                   L"def add(a, b):\n"
                                   L"    return a + b\n");
    Value sum3 = make_test_function(test_context, L"sum3",
                                    L"def sum3(a, b, c):\n"
                                    L"    return a + b + c\n");
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    EXPECT_EQ(Value::from_smi(7),
              test_context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(zero)));
    EXPECT_EQ(
        Value::from_smi(11),
        test_context.thread()->call_clovervm_function(
            TValue<Function>::from_value_assumed(inc), Value::from_smi(10)));
    EXPECT_EQ(Value::from_smi(30),
              test_context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(add),
                  Value::from_smi(10), Value::from_smi(20)));
    EXPECT_EQ(Value::from_smi(60),
              test_context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(sum3),
                  Value::from_smi(10), Value::from_smi(20),
                  Value::from_smi(30)));
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, call_clovervm_function_returns_pending_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value function = make_test_function(test_context, L"f",
                                        L"def f(a):\n"
                                        L"    raise ValueError\n");
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    Value actual = test_context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(function), Value::from_smi(10));

    EXPECT_TRUE(actual.is_exception_marker());
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_TRUE(test_context.thread()->has_pending_exception());
    EXPECT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    test_context.thread()->clear_pending_exception();
}

TEST(Interpreter, call_clovervm_function_uses_function_call_adaptation)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value function = make_test_function(test_context, L"f",
                                        L"def f(a, b=32):\n"
                                        L"    return a + b\n");
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    Value actual = test_context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(function), Value::from_smi(10));

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, call_clovervm_method_binds_class_function_receiver)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"MethodSource"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *instance =
        test_context.thread()->make_internal_raw<Instance>(cls);
    Value method = make_test_function(test_context, L"method",
                                      L"def method(self, value):\n"
                                      L"    return value + 5\n");
    ASSERT_TRUE(cls->set_own_property(method_name, method));
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    Value actual = test_context.thread()->call_clovervm_method(
        Value::from_oop(instance), method_name, Value::from_smi(37));

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, call_clovervm_method_calls_unbound_own_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"MethodSource"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *instance =
        test_context.thread()->make_internal_raw<Instance>(cls);
    Value method = make_test_function(test_context, L"method",
                                      L"def method():\n"
                                      L"    return 42\n");
    ASSERT_TRUE(instance->set_own_property(method_name, method));
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    Value actual = test_context.thread()->call_clovervm_method(
        Value::from_oop(instance), method_name);

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, call_clovervm_method_uses_function_call_adaptation)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"MethodSource"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *instance =
        test_context.thread()->make_internal_raw<Instance>(cls);
    Value method = make_test_function(test_context, L"method",
                                      L"def method(self, value=32):\n"
                                      L"    return value + 10\n");
    ASSERT_TRUE(cls->set_own_property(method_name, method));
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    Value actual = test_context.thread()->call_clovervm_method(
        Value::from_oop(instance), method_name);

    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, value_repr_uses_special_method_lookup)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"ReprSource"));
    TValue<String> dunder_repr_name(
        test_context.vm().get_or_create_interned_string_value(L"__repr__"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *instance =
        test_context.thread()->make_internal_raw<Instance>(cls);
    Value class_repr = make_test_function(test_context, L"class_repr",
                                          L"def class_repr(self):\n"
                                          L"    return 'class repr'\n");
    Value instance_repr = make_test_function(test_context, L"instance_repr",
                                             L"def instance_repr():\n"
                                             L"    return 'instance repr'\n");
    ASSERT_TRUE(cls->set_own_property(dunder_repr_name, class_repr));
    ASSERT_TRUE(instance->set_own_property(dunder_repr_name, instance_repr));

    Value actual = value_to_repr_string(Value::from_oop(instance));

    ASSERT_FALSE(actual.is_exception_marker());
    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(L"class repr",
                 string_as_wchar_t(TValue<String>::from_value_assumed(actual)));
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, call_clovervm_method_calls_inline_value_native_methods)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> dunder_str_name(
        test_context.vm().get_or_create_interned_string_value(L"__str__"));
    TValue<String> dunder_repr_name(
        test_context.vm().get_or_create_interned_string_value(L"__repr__"));

    auto expect_method_string = [&](Value receiver, TValue<String> name,
                                    const wchar_t *expected) {
        Value actual =
            test_context.thread()->call_clovervm_method(receiver, name);

        ASSERT_FALSE(actual.is_exception_marker());
        ASSERT_TRUE(can_convert_to<String>(actual));
        EXPECT_STREQ(expected, string_as_wchar_t(
                                   TValue<String>::from_value_assumed(actual)));
        EXPECT_FALSE(test_context.thread()->has_pending_exception());
    };

    expect_method_string(Value::from_smi(42), dunder_str_name, L"42");
    expect_method_string(Value::from_smi(-7), dunder_repr_name, L"-7");
    expect_method_string(Value::True(), dunder_str_name, L"True");
    expect_method_string(Value::False(), dunder_repr_name, L"False");
    expect_method_string(Value::None(), dunder_str_name, L"None");
    expect_method_string(Value::None(), dunder_repr_name, L"None");
    expect_method_string(Value::NotImplemented(), dunder_str_name,
                         L"NotImplemented");
    expect_method_string(Value::NotImplemented(), dunder_repr_name,
                         L"NotImplemented");
    expect_method_string(Value::Ellipsis(), dunder_str_name, L"Ellipsis");
    expect_method_string(Value::Ellipsis(), dunder_repr_name, L"Ellipsis");
}

TEST(Interpreter, builtin_singletons_have_names_types_and_truthiness)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::NotImplemented(),
              test_context.run_file(L"NotImplemented\n"));
    EXPECT_EQ(Value::Ellipsis(), test_context.run_file(L"Ellipsis\n"));
    EXPECT_EQ(Value::Ellipsis(), test_context.run_file(L"...\n"));

    EXPECT_STREQ(
        L"NotImplementedType",
        string_as_wchar_t(
            test_context.vm().not_implemented_type_class()->get_name()));
    EXPECT_STREQ(
        L"ellipsis",
        string_as_wchar_t(test_context.vm().ellipsis_type_class()->get_name()));

    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"if NotImplemented:\n"
                                                        L"    x = 1\n"
                                                        L"else:\n"
                                                        L"    x = 0\n"
                                                        L"x\n"));
    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"if Ellipsis:\n"
                                                        L"    x = 1\n"
                                                        L"else:\n"
                                                        L"    x = 0\n"
                                                        L"x\n"));
}

TEST(Interpreter, call_clovervm_method_calls_builtin_repr_methods)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> dunder_str_name(
        test_context.vm().get_or_create_interned_string_value(L"__str__"));
    TValue<String> dunder_repr_name(
        test_context.vm().get_or_create_interned_string_value(L"__repr__"));

    auto expect_method_string = [&](Value receiver, TValue<String> name,
                                    const wchar_t *expected) {
        Value actual =
            test_context.thread()->call_clovervm_method(receiver, name);

        ASSERT_FALSE(actual.is_exception_marker());
        ASSERT_TRUE(can_convert_to<String>(actual));
        EXPECT_STREQ(expected, string_as_wchar_t(
                                   TValue<String>::from_value_assumed(actual)));
        EXPECT_FALSE(test_context.thread()->has_pending_exception());
    };

    TValue<String> string =
        test_context.vm().get_or_create_interned_string_value(L"a'b\n");
    expect_method_string(string.raw_value(), dunder_repr_name, L"'a\\'b\\n'");

    List *list = test_context.thread()->make_object_raw<List>();
    list->append(Value::from_smi(42));
    list->append(Value::True());
    list->append(Value::None());
    list->append(string.raw_value());
    expect_method_string(Value::from_oop(list), dunder_repr_name,
                         L"[42, True, None, 'a\\'b\\n']");
    expect_method_string(Value::from_oop(list), dunder_str_name,
                         L"[42, True, None, 'a\\'b\\n']");

    Tuple *empty_tuple = test_context.thread()->make_object_raw<Tuple>(0);
    expect_method_string(Value::from_oop(empty_tuple), dunder_repr_name, L"()");

    Tuple *singleton_tuple = test_context.thread()->make_object_raw<Tuple>(1);
    singleton_tuple->initialize_item_unchecked(0, Value::from_smi(42));
    expect_method_string(Value::from_oop(singleton_tuple), dunder_repr_name,
                         L"(42,)");

    Tuple *tuple = test_context.thread()->make_object_raw<Tuple>(2);
    tuple->initialize_item_unchecked(0, Value::from_smi(42));
    tuple->initialize_item_unchecked(1, string.raw_value());
    expect_method_string(Value::from_oop(tuple), dunder_str_name,
                         L"(42, 'a\\'b\\n')");

    Dict *dict = test_context.thread()->make_object_raw<Dict>();
    TValue<String> alpha =
        test_context.vm().get_or_create_interned_string_value(L"alpha");
    TValue<String> beta =
        test_context.vm().get_or_create_interned_string_value(L"beta");
    TValue<String> removed =
        test_context.vm().get_or_create_interned_string_value(L"removed");
    ASSERT_FALSE(
        dict->set_item_for_str(test_context.thread(), alpha, Value::from_smi(1))
            .has_exception());
    ASSERT_FALSE(dict->set_item_for_str(test_context.thread(), removed,
                                        Value::from_smi(99))
                     .has_exception());
    ASSERT_FALSE(
        dict->set_item_for_str(test_context.thread(), beta, Value::True())
            .has_exception());
    ASSERT_FALSE(
        dict->del_item_for_str(test_context.thread(), removed).has_exception());
    expect_method_string(Value::from_oop(dict), dunder_repr_name,
                         L"{'alpha': 1, 'beta': True}");
    expect_method_string(Value::from_oop(dict), dunder_str_name,
                         L"{'alpha': 1, 'beta': True}");

    Dict *reordered_dict = test_context.thread()->make_object_raw<Dict>();
    ASSERT_FALSE(
        reordered_dict
            ->set_item_for_str(test_context.thread(), beta, Value::True())
            .has_exception());
    ASSERT_FALSE(
        reordered_dict
            ->set_item_for_str(test_context.thread(), alpha, Value::from_smi(1))
            .has_exception());
    Value dict_str = test_context.thread()->call_clovervm_method(
        Value::from_oop(dict), dunder_str_name);
    Value reordered_dict_str = test_context.thread()->call_clovervm_method(
        Value::from_oop(reordered_dict), dunder_str_name);
    ASSERT_FALSE(dict_str.is_exception_marker());
    ASSERT_FALSE(reordered_dict_str.is_exception_marker());
    ASSERT_TRUE(can_convert_to<String>(dict_str));
    ASSERT_TRUE(can_convert_to<String>(reordered_dict_str));
    EXPECT_NE(dict_str, reordered_dict_str);
    EXPECT_STREQ(L"{'beta': True, 'alpha': 1}",
                 string_as_wchar_t(
                     TValue<String>::from_value_assumed(reordered_dict_str)));
    EXPECT_FALSE(test_context.thread()->has_pending_exception());

    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"Plain"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *instance =
        test_context.thread()->make_internal_raw<Instance>(cls);
    expect_method_string(Value::from_oop(instance), dunder_repr_name,
                         L"<Plain object>");
    expect_method_string(Value::from_oop(instance), dunder_str_name,
                         L"<Plain object>");
}

TEST(Interpreter, call_clovervm_method_reports_missing_method)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"MethodSource"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *instance =
        test_context.thread()->make_internal_raw<Instance>(cls);
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    Value actual = test_context.thread()->call_clovervm_method(
        Value::from_oop(instance), method_name);

    EXPECT_TRUE(actual.is_exception_marker());
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    expect_thread_python_error(test_context.thread(), L"AttributeError", L"");
    test_context.thread()->clear_pending_exception();
}

TEST(Interpreter, call_clovervm_method_reports_non_callable_method)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"MethodSource"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    Instance *instance =
        test_context.thread()->make_internal_raw<Instance>(cls);
    ASSERT_TRUE(cls->set_own_property(method_name, Value::from_smi(7)));
    Value *caller_fp = test_context.thread()->clover_frame_frontier();

    Value actual = test_context.thread()->call_clovervm_method(
        Value::from_oop(instance), method_name);

    EXPECT_TRUE(actual.is_exception_marker());
    EXPECT_EQ(caller_fp, test_context.thread()->clover_frame_frontier());
    expect_thread_python_error(test_context.thread(), L"TypeError",
                               L"object is not callable");
    test_context.thread()->clear_pending_exception();
}

TEST(Interpreter, call_intrinsic_one_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_increment(41)\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_increment",
        make_intrinsic_function(&test_context.vm(), native_increment));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, call_intrinsic_two_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_add(20, 22)\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_add",
        make_intrinsic_function(&test_context.vm(), native_add));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, builtin_intrinsic_method_defaults_are_used)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> class_name(
        test_context.vm().get_or_create_interned_string_value(L"Builtin"));
    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        class_name, 2, test_context.vm().object_class(),
        NativeLayoutId::Instance);
    TValue<Tuple> defaults = test_context.thread()->make_object_value<Tuple>(1);
    defaults.extract()->initialize_item_unchecked(0, Value::from_smi(30));
    BuiltinIntrinsicMethod methods[] = {with_defaults(
        builtin_intrinsic_method(L"add3", native_add_three), defaults)};
    ASSERT_TRUE(install_builtin_intrinsic_methods(&test_context.vm(), cls,
                                                  methods, std::size(methods))
                    .has_value());

    CodeObject *code_obj = test_context.compile_file(L"Builtin.add3(5, 7)\n");
    store_global_to_module_for_test(test_context, code_obj, L"Builtin",
                                    Value::from_oop(cls));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, native_exception_marker_materializes_stop_iteration)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_stop()\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_stop",
        make_intrinsic_function(&test_context.vm(),
                                native_stop_iteration_with_value));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    EXPECT_EQ(L"StopIteration: 123",
              format_pending_python_exception(test_context.thread()));

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<Exception> exception =
        test_context.thread()->pending_exception_object();
    ASSERT_TRUE(can_convert_to<StopIterationObject>(exception.raw_value()));
    StopIterationObject *stop_iteration =
        exception.raw_value().get_ptr<StopIterationObject>();
    EXPECT_EQ(Value::from_smi(123), stop_iteration->value);
}

TEST(Interpreter, native_exception_marker_unwinds_nested_frames)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"def call_stop():\n"
                                                     L"    native_stop()\n"
                                                     L"    return 99\n"
                                                     L"call_stop()\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_stop",
        make_intrinsic_function(&test_context.vm(),
                                native_stop_iteration_with_value));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    EXPECT_EQ(L"StopIteration: 123",
              format_pending_python_exception(test_context.thread()));

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<Exception> exception =
        test_context.thread()->pending_exception_object();
    ASSERT_TRUE(can_convert_to<StopIterationObject>(exception.raw_value()));
    StopIterationObject *stop_iteration =
        exception.raw_value().get_ptr<StopIterationObject>();
    EXPECT_EQ(Value::from_smi(123), stop_iteration->value);
}

TEST(Interpreter, catch_stop_iteration_as_exposes_value)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"result = 0\n"
                                  L"try:\n"
                                  L"    native_stop()\n"
                                  L"except StopIteration as e:\n"
                                  L"    result = e.value\n"
                                  L"result\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_stop",
        make_intrinsic_function(&test_context.vm(),
                                native_stop_iteration_with_value));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(123), actual);
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, raise_from_handler_sets_exception_context)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"try:\n"
                                                     L"    raise NameError\n"
                                                     L"except Exception:\n"
                                                     L"    raise ValueError\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<Exception> exception =
        test_context.thread()->pending_exception_object();
    EXPECT_EQ(test_context.thread()->class_for_builtin_name(L"ValueError"),
              exception.extract()->get_shape()->get_class());

    TValue<String> context_name =
        test_context.vm().get_or_create_interned_string_value(L"__context__");
    Value context = exception.extract()->get_own_property(context_name);
    ASSERT_TRUE(can_convert_to<ExceptionObject>(context));
    EXPECT_EQ(test_context.thread()->class_for_builtin_name(L"NameError"),
              context.get_ptr<ExceptionObject>()->get_shape()->get_class());
}

TEST(Interpreter, bare_raise_without_active_exception_raises_runtime_error)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"raise\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"RuntimeError",
                               L"No active exception to reraise");

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<Exception> exception =
        test_context.thread()->pending_exception_object();
    EXPECT_EQ(test_context.thread()->class_for_builtin_name(L"RuntimeError"),
              exception.extract()->get_shape()->get_class());
}

TEST(Interpreter, try_finally_raise_chains_body_exception_as_context)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"try:\n"
                                                     L"    raise NameError\n"
                                                     L"finally:\n"
                                                     L"    raise ValueError\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<Exception> exception =
        test_context.thread()->pending_exception_object();
    EXPECT_EQ(test_context.thread()->class_for_builtin_name(L"ValueError"),
              exception.extract()->get_shape()->get_class());

    TValue<String> context_name =
        test_context.vm().get_or_create_interned_string_value(L"__context__");
    Value context = exception.extract()->get_own_property(context_name);
    ASSERT_TRUE(can_convert_to<ExceptionObject>(context));
    EXPECT_EQ(test_context.thread()->class_for_builtin_name(L"NameError"),
              context.get_ptr<ExceptionObject>()->get_shape()->get_class());
}

TEST(Interpreter, bare_raise_in_exceptional_finally_reraises_body_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"try:\n"
                                                     L"    raise NameError\n"
                                                     L"finally:\n"
                                                     L"    raise\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"NameError", L"");

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<Exception> exception =
        test_context.thread()->pending_exception_object();
    EXPECT_EQ(test_context.thread()->class_for_builtin_name(L"NameError"),
              exception.extract()->get_shape()->get_class());
}

TEST(Interpreter,
     exception_argument_formatting_is_deferred_and_preserves_pending_error)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    CodeObject *code = context.compile_file(L"class Argument:\n"
                                            L"    def __init__(self):\n"
                                            L"        self.calls = 0\n"
                                            L"    def __str__(self):\n"
                                            L"        self.calls += 1\n"
                                            L"        raise ValueError\n"
                                            L"arg = Argument()\n");
    ASSERT_FALSE(thread->run_clovervm_code_object(code).is_exception_marker());
    Value arg = load_global_from_module_for_test(code, interned_string(L"arg"));
    TValue<ClassObject> cls = TValue<ClassObject>::from_oop(
        thread->class_for_builtin_name(L"TypeError"));
    Owned<TValue<Exception>> exception(
        make_exception_object_with_arg(thread, cls, arg));
    EXPECT_EQ(Value::from_smi(0), load_attr(arg, interned_string(L"calls")));
    EXPECT_EQ(arg, exception.extract()->args.extract()->item_unchecked(0));
    EXPECT_FALSE(thread->has_pending_exception());
    (void)thread->set_pending_exception_object(exception.value());
    EXPECT_EQ(L"TypeError: <exception str() failed>",
              format_pending_python_exception(thread));
    EXPECT_EQ(Value::from_smi(1), load_attr(arg, interned_string(L"calls")));
    ASSERT_EQ(PendingExceptionKind::Object, thread->pending_exception_kind());
    EXPECT_EQ(exception.value(), thread->pending_exception_object());
    EXPECT_EQ(L"TypeError: <exception str() failed>",
              format_pending_python_exception(thread));
    EXPECT_EQ(exception.value(), thread->pending_exception_object());
}

TEST(Interpreter, unhandled_pending_exception_reports_class_and_message)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_boom()\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_boom",
        make_intrinsic_function(&test_context.vm(),
                                native_base_exception_with_message));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"BaseException",
                               L"boom");
}

TEST(Interpreter, native_exception_marker_requires_pending_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_broken()\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_broken",
        make_intrinsic_function(&test_context.vm(),
                                native_marker_without_pending_exception));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"SystemError",
                               L"exception marker without pending exception");
}

TEST(Interpreter, raise_unwind_raises_exception_class)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value exception_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"Exception"));
    CodeObject *code_obj =
        make_raise_unwind_code(test_context, exception_class);

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"Exception", L"");
}

TEST(Interpreter, raise_unwind_raises_exception_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<Exception> exception = make_exception_object_with_string(
        active_thread(),
        TValue<ClassObject>::from_oop(
            test_context.thread()->class_for_builtin_name(L"ValueError")),
        L"boom");
    CodeObject *code_obj =
        make_raise_unwind_code(test_context, exception.raw_value());

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"boom");
}

TEST(Interpreter, raise_unwind_rejects_non_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        make_raise_unwind_code(test_context, Value::from_smi(1));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"TypeError",
                               L"exceptions must derive from BaseException");
}

TEST(Interpreter, builtin_module_lookup)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"range\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"range");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(NativeLayoutId::Function,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(actual, load_global_from_module_for_test(code_obj, name));
}

TEST(Interpreter,
     builtins_module_attribute_read_write_delete_use_module_storage)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj =
        test_context.compile_file(L"result = __builtins__.range(1)\n"
                                  L"__builtins__.module_attr_probe = 42\n"
                                  L"written = __builtins__.module_attr_probe\n"
                                  L"del __builtins__.module_attr_probe\n");

    Value run_result =
        test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_FALSE(run_result.is_exception_marker());

    TValue<String> result_name =
        test_context.vm().get_or_create_interned_string_value(L"result");
    Value result = load_global_from_module_for_test(code_obj, result_name);
    ASSERT_TRUE(result.is_ptr());
    EXPECT_EQ(NativeLayoutId::RangeIterator,
              result.get_ptr<Object>()->native_layout_id());

    TValue<String> written_name =
        test_context.vm().get_or_create_interned_string_value(L"written");
    EXPECT_EQ(Value::from_smi(42),
              load_global_from_module_for_test(code_obj, written_name));

    TValue<String> probe_name =
        test_context.vm().get_or_create_interned_string_value(
            L"module_attr_probe");
    ModuleObject *builtins =
        test_context.vm().global_builtins_module().extract();
    EXPECT_EQ(Value::not_present(), builtins->get_own_property(probe_name));
}

TEST(Interpreter, trusted_python_builtins_are_installed)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    ModuleObject *builtins =
        test_context.vm().global_builtins_module().extract();

    struct ExpectedBuiltin
    {
        const wchar_t *name;
        const wchar_t *docstring;
    };

    constexpr ExpectedBuiltin expected_builtins[] = {
        {L"iter",
         L"iter(iterable) -> iterator\n"
         L"iter(callable, sentinel) -> iterator\n"
         L"\n"
         L"Get an iterator from an object.  In the first form, the argument "
         L"must\n"
         L"supply its own iterator, or be a sequence.\n"
         L"In the second form, the callable is called until it returns the "
         L"sentinel."},
        {L"next",
         L"next(iterator[, default])\n"
         L"\n"
         L"Return the next item from the iterator. If default is given and "
         L"the iterator\n"
         L"is exhausted, it is returned instead of raising StopIteration."},
        {L"repr",
         L"Return the canonical string representation of the object.\n"
         L"\n"
         L"For many object types, including most builtins, eval(repr(obj)) "
         L"== obj."},
        {L"hash", L"Return the hash value for the object."},
        {L"len", L"Return the number of items in a container."},
        {L"globals",
         L"Return the dictionary containing the current scope's global "
         L"variables."},
        {L"print", L"print(*args, sep=' ', end='\\n', file=None, flush=False)\n"
                   L"\n"
                   L"Print the values to standard output."},
    };

    for(const ExpectedBuiltin &expected: expected_builtins)
    {
        TValue<String> name_value =
            test_context.vm().get_or_create_interned_string_value(
                expected.name);
        Value value = load_module_global(builtins, name_value);
        ASSERT_TRUE(value.is_ptr());
        EXPECT_EQ(NativeLayoutId::Function,
                  value.get_ptr<Object>()->native_layout_id());
        TValue<Function> function = TValue<Function>::from_value_assumed(value);
        Optional<TValue<String>> docstring =
            function.extract()->docstring.value();
        ASSERT_TRUE(docstring.has_value());
        EXPECT_STREQ(expected.docstring,
                     cl_test_string_to_wstring(docstring.value()).c_str());
    }
}

TEST(Interpreter, membership_fallback_helper_is_vm_owned_and_hidden)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<Function> iter_fallback =
        test_context.vm().membership_iter_fallback_function();
    TValue<Function> sequence_fallback =
        test_context.vm().membership_sequence_fallback_function();
    EXPECT_EQ(NativeLayoutId::Function,
              iter_fallback.raw_value().get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(
        NativeLayoutId::Function,
        sequence_fallback.raw_value().get_ptr<Object>()->native_layout_id());

    TValue<String> iter_helper_name =
        test_context.vm().get_or_create_interned_string_value(
            L"_clover_iter_membership_fallback");
    TValue<String> sequence_helper_name =
        test_context.vm().get_or_create_interned_string_value(
            L"_clover_sequence_membership_fallback");
    ModuleObject *builtins =
        test_context.vm().global_builtins_module().extract();
    EXPECT_EQ(Value::not_present(),
              builtins->get_own_property(iter_helper_name));
    EXPECT_EQ(Value::not_present(),
              builtins->get_own_property(sequence_helper_name));
    expect_python_error(L"_clover_iter_membership_fallback\n", L"NameError",
                        L"name '_clover_iter_membership_fallback' is not "
                        L"defined");
    expect_python_error(L"_clover_sequence_membership_fallback\n", L"NameError",
                        L"name '_clover_sequence_membership_fallback' is not "
                        L"defined");
}

TEST(Interpreter, builtin_module_exposes_singleton_values)
{
    test::VmTestContext test_context;
    ModuleObject *builtins =
        test_context.vm().global_builtins_module().extract();

    EXPECT_EQ(
        Value::True(),
        builtins->get_own_property(
            test_context.vm().get_or_create_interned_string_value(L"True")));
    EXPECT_EQ(
        Value::False(),
        builtins->get_own_property(
            test_context.vm().get_or_create_interned_string_value(L"False")));
    EXPECT_EQ(
        Value::None(),
        builtins->get_own_property(
            test_context.vm().get_or_create_interned_string_value(L"None")));
}

TEST(Interpreter, user_code_cannot_use_clover_call_special_as_intrinsic)
{
    expect_python_error(
        L"__clover_call_special__(1, \"__repr__\", TypeError, \"missing\")\n",
        L"NameError", L"name '__clover_call_special__' is not defined");
}

TEST(Interpreter, user_code_cannot_use_clover_write_stdout_as_intrinsic)
{
    expect_python_error(L"__clover_write_stdout__(\"hello\")\n", L"NameError",
                        L"name '__clover_write_stdout__' is not defined");
}

TEST(Interpreter, user_defined_clover_write_stdout_name_is_ordinary_function)
{
    CapturedStdoutRun run =
        run_file_with_captured_stdout(L"def __clover_write_stdout__(value):\n"
                                      L"    return 456\n"
                                      L"__clover_write_stdout__(\"hello\")\n");

    EXPECT_EQ(Value::from_smi(456), run.return_value);
    EXPECT_TRUE(run.stdout_text.empty());
}

TEST(Interpreter, user_code_cannot_use_clover_globals_as_intrinsic)
{
    expect_python_error(L"__clover_globals__()\n", L"NameError",
                        L"name '__clover_globals__' is not defined");
}

TEST(Interpreter, user_code_cannot_use_clover_locals_as_intrinsic)
{
    expect_python_error(L"__clover_locals__()\n", L"NameError",
                        L"name '__clover_locals__' is not defined");
}

TEST(Interpreter, user_code_cannot_use_clover_ternary_pow_as_intrinsic)
{
    expect_python_error(L"__clover_ternary_pow__(2, 3, 5)\n", L"NameError",
                        L"name '__clover_ternary_pow__' is not defined");
}

TEST(Interpreter, globals_slotdict_excludes_builtin_fallbacks)
{
    expect_python_error(L"globals()[\"len\"]\n", L"KeyError", L"");
}

TEST(Interpreter, main_module_file_sets_file_not_name)
{
    test::VmTestContext test_context;

    CodeObject *code = test_context.thread()
                           ->compile(L"(__name__, __file__)\n", StartRule::File,
                                     L"/tmp/script.py")
                           .value();
    Value result = test_context.thread()->run_clovervm_code_object(code);
    ASSERT_TRUE(can_convert_to<Tuple>(result));
    Tuple *tuple = result.get_ptr<Tuple>();
    ASSERT_EQ(2, tuple->size());

    Value name = tuple->item_unchecked(0);
    ASSERT_TRUE(can_convert_to<String>(name));
    EXPECT_STREQ(L"__main__",
                 string_as_wchar_t(TValue<String>::from_value_assumed(name)));

    Value file = tuple->item_unchecked(1);
    ASSERT_TRUE(can_convert_to<String>(file));
    EXPECT_STREQ(L"/tmp/script.py",
                 string_as_wchar_t(TValue<String>::from_value_assumed(file)));
}

TEST(Interpreter, globals_slotdict_rejects_non_string_keys)
{
    expect_python_error(L"globals()[1] = 2\n", L"TypeError",
                        L"slotdict keys must be strings");
    expect_python_error(L"globals()[1]\n", L"TypeError",
                        L"slotdict keys must be strings");
    expect_python_error(L"del globals()[1]\n", L"TypeError",
                        L"slotdict keys must be strings");
}

TEST(Interpreter, globals_slotdict_class_is_not_builtin_binding)
{
    expect_python_error(L"slotdict\n", L"NameError",
                        L"name 'slotdict' is not defined");
}

TEST(Interpreter, locals_slotdict_excludes_builtin_fallbacks)
{
    expect_python_error(L"locals()[\"len\"]\n", L"KeyError", L"");
}

TEST(Interpreter, locals_builtin_is_unimplemented_in_function_scope)
{
    expect_python_error(L"def f():\n"
                        L"    return locals()\n"
                        L"f()\n",
                        L"UnimplementedError",
                        L"locals() is only implemented for module scope");
}

TEST(Interpreter, dir_without_arguments_propagates_locals_error)
{
    expect_python_error(L"dir()\n", L"UnimplementedError",
                        L"locals() is only implemented for module scope");
}

TEST(Interpreter, slotdict_hides_virtual_special_descriptors)
{
    expect_python_error(L"class C:\n"
                        L"    pass\n"
                        L"c = C()\n"
                        L"c.__dict__[\"__class__\"]\n",
                        L"KeyError", L"");
    expect_python_error(L"class C:\n"
                        L"    pass\n"
                        L"c = C()\n"
                        L"c.__dict__[\"__dict__\"]\n",
                        L"KeyError", L"");
}

TEST(Interpreter,
     native_allocator_selection_precedes_constructor_argument_binding)
{
    expect_python_error(L"class C(tuple): pass\nC(1)\n", L"TypeError",
                        L"native instance layout requires a builtin allocator");
    expect_python_error(L"class C(tuple):\n"
                        L"    def __init__(self, *, required):\n"
                        L"        raise AssertionError\n"
                        L"C()\n",
                        L"TypeError",
                        L"native instance layout requires a builtin allocator");
}

TEST(Interpreter, exception_default_allocation_preserves_native_storage)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
assert BaseException().__class__ is BaseException
assert Exception().__class__ is Exception
assert ValueError().__class__ is ValueError
assert isinstance(Exception().args, tuple)
assert len(Exception().args) == 0
class E(Exception): pass
class F(E): pass
f = F()
assert f.__class__ is F
assert len(f.args) == 0
f.extra = 42
assert f.extra == 42
assert f.__dict__["extra"] == 42
class G(Exception):
    def __init__(self, value=7, *, other=9):
        assert len(self.args) == 0
        self.value = value
        self.other = other
assert G().value == 7
g = G(11, other=13)
assert g.value == 11
assert g.other == 13
assert len(g.args) == 0
try:
    raise f
except F as caught:
    assert caught is f
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, exception_default_allocation_propagates_initializer_errors)
{
    expect_python_error(L"class E(Exception):\n"
                        L"    def __init__(self):\n"
                        L"        raise ValueError\n"
                        L"E()\n",
                        L"ValueError", L"");
    expect_python_error(L"class E(Exception):\n"
                        L"    def __init__(self):\n"
                        L"        return 1\n"
                        L"E()\n",
                        L"TypeError",
                        L"__init__ should return None, not a value");
}

TEST(Interpreter, stop_iteration_default_allocation_preserves_native_storage)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
s = StopIteration()
assert s.__class__ is StopIteration
assert isinstance(s.args, tuple)
assert len(s.args) == 0
assert s.value is None
class E(Exception): pass
class C(E, StopIteration): pass
class D(C): pass
class S(StopIteration): pass
class F(S, Exception): pass
for cls in (C, D, F):
    s = cls()
    assert s.__class__ is cls
    assert len(s.args) == 0
    assert s.value is None
    s.extra = 42
    assert s.extra == 42
    assert s.__dict__["extra"] == 42
class G(StopIteration):
    def __init__(self, value=7, *, other=9):
        assert self.value is None
        assert len(self.args) == 0
        self.extra = value
        self.other = other
class H(G): pass
assert H().extra == 7
s = H(11, other=13)
assert s.extra == 11
assert s.other == 13
assert s.value is None
try:
    raise s
except H as caught:
    assert caught is s
    assert caught.value is None
def replacement(self, value):
    self.extra = value + 1
G.__init__ = replacement
assert H(20).extra == 21
del G.__init__
s = H()
assert s.__class__ is H
assert s.value is None
assert len(s.args) == 0
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter,
     stop_iteration_default_allocation_propagates_initializer_errors)
{
    expect_python_error(L"class S(StopIteration):\n"
                        L"    def __init__(self):\n"
                        L"        raise ValueError\n"
                        L"S()\n",
                        L"ValueError", L"");
    expect_python_error(L"class S(StopIteration):\n"
                        L"    def __init__(self):\n"
                        L"        return 1\n"
                        L"S()\n",
                        L"TypeError",
                        L"__init__ should return None, not a value");
}

TEST(Interpreter, native_exception_initializers_pack_arguments)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"PY(
class E(Exception): pass
class S(StopIteration): pass
class Mixed(E, StopIteration): pass
for cls in (BaseException, Exception, ValueError, E, StopIteration, S, Mixed):
    for args in ((), (None,), ("hello",), ("hello", 42)):
        # Explicit arities exercise the constructor binder without unpacking.
        if len(args) == 0:
            error = cls()
        elif len(args) == 1:
            error = cls(args[0])
        else:
            error = cls(args[0], args[1])
        assert error.__class__ is cls
        assert len(error.args) == len(args)
        for index in range(len(args)):
            assert error.args[index] is args[index]
        assert isinstance(error.args, tuple)
        if isinstance(error, StopIteration):
            if len(args):
                assert error.value is args[0]
            else:
                assert error.value is None
        error.extra = 17
        assert error.__dict__["extra"] == 17
        try:
            raise error
        except cls as caught:
            assert caught is error
assert str(Exception()) == ""
assert str(Exception("hello")) == "hello"
assert str(Exception("hello", 42)) == "('hello', 42)"
assert repr(Exception("hello", 42)) == "Exception('hello', 42)"
assert repr(StopIteration(42)) == "StopIteration(42)"
)PY");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, native_exception_initializers_support_reinitialization)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class S(StopIteration): pass
s = S("old")
assert s.__init__("new", 42) is None
assert len(s.args) == 2
assert s.args[0] == "new"
assert s.args[1] == 42
assert s.value == "new"
assert BaseException.__init__(s, "base") is None
assert len(s.args) == 1
assert s.args[0] == "base"
assert s.value == "new"
assert Exception.__init__(s) is None
assert len(s.args) == 0
assert s.value == "new"
assert StopIteration.__init__(s) is None
assert len(s.args) == 0
assert s.value is None
item = [1, 2]
assert s.__init__(item, item) is None
assert s.args[0] is item
assert s.args[1] is item
assert s.value is item
# The stored fields retain their values after the callback frame is gone.
del item
assert len(s.value) == 2
assert s.value[0] == 1
assert s.value[1] == 2
assert s.__init__() is None
assert s.value is None
error = Exception("old")
assert error.__init__(1, 2, 3) is None
assert len(error.args) == 3
assert error.args[0] == 1
assert error.args[1] == 2
assert error.args[2] == 3
assert error.__init__() is None
assert len(error.args) == 0
try:
    error.args = (1,)
except AttributeError:
    pass
else:
    raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, native_exception_initializers_validate_receivers)
{
    expect_python_error(
        L"BaseException.__init__(object())\n", L"TypeError",
        L"BaseException.__init__ expects an exception receiver");
    expect_python_error(
        L"Exception.__init__(1)\n", L"TypeError",
        L"BaseException.__init__ expects an exception receiver");
    expect_python_error(
        L"StopIteration.__init__(Exception())\n", L"TypeError",
        L"StopIteration.__init__ expects a StopIteration receiver");
    expect_python_error(
        L"StopIteration.__init__(None)\n", L"TypeError",
        L"StopIteration.__init__ expects a StopIteration receiver");
}

TEST(Interpreter,
     native_exception_initializers_reject_keywords_without_mutation)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class E(Exception): pass
class S(StopIteration): pass
for cls in (BaseException, Exception, StopIteration, E, S):
    try:
        cls(args=1)
    except TypeError:
        pass
    else:
        raise AssertionError
    error = cls("old")
    try:
        error.__init__("new", value=42)
    except TypeError:
        pass
    else:
        raise AssertionError
    assert len(error.args) == 1
    assert error.args[0] == "old"
    if isinstance(error, StopIteration):
        assert error.value == "old"
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, native_exception_initializer_inheritance_invalidates_thunks)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class E(Exception): pass
class F(E): pass
class S(StopIteration): pass
class T(S): pass
f = F(1, 2)
assert len(f.args) == 2
assert f.args[0] == 1
assert f.args[1] == 2
assert T(1, 2).value == 1
def replacement(self, value):
    BaseException.__init__(self, value + 1)
    self.extra = value
E.__init__ = replacement
S.__init__ = replacement
f = F(7)
assert len(f.args) == 1
assert f.args[0] == 8
s = T(7)
assert len(s.args) == 1
assert s.args[0] == 8
assert s.value is None
assert s.extra == 7
del E.__init__
del S.__init__
f = F(9, 10)
assert len(f.args) == 2
assert f.args[0] == 9
assert f.args[1] == 10
s = T(9, 10)
assert len(s.args) == 2
assert s.args[0] == 9
assert s.args[1] == 10
assert s.value == 9
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_initializer_preserves_class_payload_and_attributes)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"PY(
class L(list): pass
class M(L): pass
for cls in (list, L, M):
    empty = cls()
    assert empty.__class__ is cls
    assert len(empty) == 0
    for source in ((1, 2), [1, 2], range(1, 3)):
        value = cls(source)
        assert value.__class__ is cls
        assert len(value) == 2
        assert value[0] == 1
        assert value[1] == 2
        if cls is not list:
            value.extra = 42
            assert value.__dict__["extra"] == 42
        assert value.copy().__class__ is list
        assert value[:].__class__ is list
        assert (value + [3]).__class__ is list
        value.append(3)
        assert value.pop() == 3
        assert len(value) == 2
class Hostile(list):
    def clear(self): raise AssertionError
    def append(self, item): raise AssertionError
h = Hostile((1, 2))
assert h[0] == 1
assert h[1] == 2
assert list.__init__(h, (3,)) is None
assert len(h) == 1
assert h[0] == 3
assert repr(h) == "[3]"
)PY");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_initializer_reinitialization_and_partial_errors)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
value = list((9,))
assert value.__init__() is None
assert len(value) == 0
assert value.__init__((1, 2)) is None
assert len(value) == 2
assert value.__init__(value) is None
assert len(value) == 0
value.__init__((1, 2))
iterator = iter(value)
value.__init__(iterator)
assert len(value) == 0
value.__init__((9,))
try:
    value.__init__(None)
except TypeError:
    pass
else:
    raise AssertionError
assert len(value) == 0
class Broken:
    def __init__(self): self.index = 0
    def __iter__(self): return self
    def __next__(self):
        self.index += 1
        if self.index == 1: return 42
        raise ValueError("broken")
value.__init__((9,))
try:
    value.__init__(Broken())
except ValueError as error:
    assert error.args[0] == "broken"
else:
    raise AssertionError
assert len(value) == 1
assert value[0] == 42
class Inspect:
    def __iter__(self):
        assert len(value) == 0
        return iter((7, 8))
value.__init__(Inspect())
assert len(value) == 2
assert value[1] == 8
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_initializer_argument_errors_precede_mutation)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
value = list((9,))
for mode in range(3):
    try:
        if mode == 0: value.__init__((1,), (2,))
        elif mode == 1: value.__init__(iterable=(1,))
        else: list(iterable=(1,))
    except TypeError:
        pass
    else:
        raise AssertionError
    assert len(value) == 1
    assert value[0] == 9
)");
    ASSERT_FALSE(result.is_exception_marker());
    expect_python_error(L"list.__init__(object())\n", L"TypeError",
                        L"list.clear expects a list receiver");
}

TEST(Interpreter,
     list_initializer_private_dependency_survives_builtin_rebinding)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
import builtins
List = list
class L(List): pass
builtins.list = None
try:
    value = L((1, 2))
    assert len(value) == 2
    assert value[1] == 2
    value.__init__((3,))
    assert len(value) == 1
    assert value[0] == 3
finally:
    builtins.list = List
assert not hasattr(builtins, "_clover_list_init")
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, list_initializer_override_and_invalidation)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class L(list): pass
class M(L): pass
assert M((1,))[0] == 1
def replacement(self, value=7, *, other=9):
    assert len(self) == 0
    list.append(self, value)
    self.other = other
L.__init__ = replacement
m = M(11, other=13)
assert m[0] == 11
assert m.other == 13
assert M()[0] == 7
del L.__init__
assert M((17,))[0] == 17
class F(list):
    def __init__(self): raise ValueError("bad")
try:
    F()
except ValueError:
    pass
else:
    raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker());
    expect_python_error(L"class L(list):\n"
                        L"    def __init__(self): return 1\nL()\n",
                        L"TypeError",
                        L"__init__ should return None, not a value");
    expect_python_error(L"class L(list):\n"
                        L"    def __new__(cls): return []\nL()\n",
                        L"TypeError", L"object is not callable");
}

TEST(Interpreter, int_subclass_construction_preserves_class_and_attributes)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"PY(
class I(int): pass
class J(I): pass
for cls in (I, J):
    assert cls().__class__ is cls
    assert cls() == 0
    for value in (0, 1, -1, True, False, 12345, -(2**61), 2**61, 2**100, -(2**100), "42", "-1267650600228229401496703205376"):
        x = cls(value)
        assert x.__class__ is cls
        assert x == int(value)
        assert str(x) == str(int(value))
        assert hash(x) == hash(int(value))
        x.extra = 42
        assert x.__dict__["extra"] == 42
        y = cls(x)
        assert y.__class__ is cls
        assert y == x
        assert y is not x
        assert not hasattr(y, "extra")
        direct = int.__new__(cls, value)
        assert direct.__class__ is cls
        assert direct == x
assert b"ab"[I(1)] == 98
assert bytes((I(97), I(98)))[1] == 98
)PY");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, int_subclass_operations_return_exact_integers)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class I(int): pass
for value in (0, 1, -1, 2**100, -(2**100)):
    x = I(value)
    x.extra = 42
    for result in (int(x), int.__new__(int, x), +x, -x, abs(x), ~x,
                   x + 0, x - 0, x * 1, x // 1, x % 3, x << 0,
                   x >> 0, x | 0, x & x, x ^ 0, x ** 1):
        assert result.__class__ is int
        assert result is not x
        assert not hasattr(result, "extra")
    assert int(x) == value
    assert +x == value
    assert abs(x) == abs(value)
    assert hash(x) == hash(value)
    assert {x: "found"}[value] == "found"
# Repeat at shared unary call sites, alternating exact ints and subclasses.
for value in (2**100, I(2**100), 7, I(7), 2**100, I(2**100)):
    assert abs(value).__class__ is int
    assert (+value).__class__ is int
class Override(I):
    def __pos__(self): return "positive override"
    def __abs__(self): return "abs override"
assert +Override(1) == "positive override"
assert abs(Override(1)) == "abs override"
large = 2**100
assert int(large) is large
assert (+large) is large
assert abs(large) is large
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, int_new_reports_class_errors_like_cpython)
{
    expect_python_error(L"int.__new__(float, 4)\n", L"TypeError",
                        L"int.__new__(float): float is not a subtype of int");
    expect_python_error(L"class F(float): pass\nint.__new__(F, 4)\n",
                        L"TypeError",
                        L"int.__new__(F): F is not a subtype of int");
    expect_python_error(L"int.__new__(bool, 4)\n", L"TypeError",
                        L"int.__new__(bool) is not safe, use bool.__new__()");
    expect_python_error(L"int.__new__(1, 4)\n", L"TypeError",
                        L"int.__new__(X): X is not a type object (int)");
    expect_python_error(L"int.__new__(None, 4)\n", L"TypeError",
                        L"int.__new__(X): X is not a type object (NoneType)");
    expect_python_error(L"class I(int): pass\nI('bad')\n", L"ValueError",
                        L"invalid literal for int()");
    expect_python_error(
        L"class I(int): pass\nI(1.5)\n", L"TypeError",
        L"int conversion is only implemented for int, bool and str");
}

TEST(Interpreter, int_subclass_new_invalidation_and_combined_boundary)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class I(int): pass
class J(I): pass
assert J(7).__class__ is J
def replacement(cls, value=9): return value
I.__new__ = replacement
assert J() == 9
assert J(11) == 11
del I.__new__
x = J(13)
assert x.__class__ is J
assert x == 13
class Combined(int):
    def __init__(self): raise AssertionError
try:
    Combined()
except TypeError:
    pass
else:
    raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, bytes_subclass_construction_and_attributes)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
class B(bytes): pass
class C(B): pass
class Mixin: pass
class Mixed(B, Mixin): pass
class I(int): pass
class L(list): pass
for cls in (bytes, B, C, Mixed):
    assert cls().__class__ is cls
    assert cls() == b""
    for source in (b"", b"abc", b"\x00\xff", [], [0, 255], (),
                   (97, 98), [I(97), I(98)], L([97, 98])):
        x = cls(source)
        assert x.__class__ is cls
        assert x == bytes(source)
        direct = bytes.__new__(cls, source)
        assert direct.__class__ is cls
        assert direct == x
        if cls is not bytes:
            x.extra = 42
            assert x.__dict__["extra"] == 42
            y = cls(x)
            assert y.__class__ is cls
            assert y is not x
            assert not hasattr(y, "extra")
original = b"abc"
assert bytes(original) is original
x = B(original)
assert bytes(x).__class__ is bytes
assert bytes(x) == original
assert bytes(x) is not x
assert {x: 42}[original] == 42
assert hash(x) == hash(original)
assert x[0] == 97
assert x[-1] == 99
assert b"b" in x
assert x.find(b"b") == 1
assert x.startswith(b"a")
for value in (x[:], x[1:], x + b"", b"" + x,
              x.replace(b"missing", b"x"), x.removeprefix(b"missing"),
              x.removesuffix(b"missing")):
    assert value.__class__ is bytes
assert len(B([0, 255])) == 2
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, bytes_new_class_and_source_errors)
{
    expect_python_error(L"bytes.__new__(int, b'x')\n", L"TypeError",
                        L"bytes.__new__(int): int is not a subtype of bytes");
    expect_python_error(L"bytes.__new__(1, b'x')\n", L"TypeError",
                        L"bytes.__new__(X): X is not a type object (int)");
    expect_python_error(L"bytes.__new__(None, b'x')\n", L"TypeError",
                        L"bytes.__new__(X): X is not a type object (NoneType)");
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
class B(bytes): pass
for cls in (bytes, B):
    for source in ([-1], [256], (2**100,), [1, -1]):
        try: cls(source)
        except ValueError as e: assert str(e) == "bytes must be in range(0, 256)"
        else: raise AssertionError
    for source in ([True], [1.0], ["x"], (None,)):
        try: cls(source)
        except TypeError as e: assert str(e) == "bytes item must be an integer"
        else: raise AssertionError
    for source in (4, "text", range(3)):
        try: cls(source)
        except TypeError as e: assert str(e) == "cannot convert object to bytes"
        else: raise AssertionError
    assert cls(None) == b""
    for mode in range(3):
        try:
            if mode == 0: cls(b"a", b"b")
            elif mode == 1: cls(source=b"a")
            else: cls("text", "utf-8")
        except TypeError: pass
        else: raise AssertionError
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, bytes_new_invalidation_and_deferred_protocol_boundary)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"py(
class B(bytes): pass
class C(B): pass
assert C(b"abc").__class__ is C
def replacement(cls, source=b"replacement"): return source
B.__new__ = replacement
assert C() == b"replacement"
assert C(11) == 11
del B.__new__
assert C(b"restored").__class__ is C
assert C(b"restored") == b"restored"
class Override(bytes):
    def __bytes__(self): raise AssertionError
x = Override(b"raw")
assert bytes(x) == b"raw"
assert B(x) == b"raw"
class Source:
    def __bytes__(self): return b"custom"
try: B(Source())
except TypeError: pass
else: raise AssertionError
class Combined(bytes):
    def __init__(self): raise AssertionError
try: Combined()
except TypeError: pass
else: raise AssertionError
)py");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, str_subclass_construction_and_attributes)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class S(str): pass
class T(S): pass
class Mixin: pass
class Mixed(S, Mixin): pass
for cls in (str, S, T, Mixed):
    assert cls().__class__ is cls
    assert cls() == ""
    for value in ("", "hello", "caf\u00e9\U0001f600", "a\x00b", 0, -123, True, 2**100):
        x = cls(value)
        assert x.__class__ is cls
        assert x == str(value)
        direct = str.__new__(cls, value)
        assert direct.__class__ is cls
        assert direct == x
        if cls is not str:
            x.extra = 42
            assert x.__dict__["extra"] == 42
            y = cls(x)
            assert y is not x
            assert y.__class__ is cls
            assert not hasattr(y, "extra")
original = "hello"
assert str(original) is original
x = S(original)
assert str(x).__class__ is str
assert str(x) is not x
assert str.__str__(original) is original
assert str.__str__(x).__class__ is str
assert str.__str__(x) == original
assert {x: 42}[original] == 42
for value in (x[:], x[0], x + "", "" + x, x.lower(), x.upper(),
              x.strip(), x.replace("missing", "x"), x.removeprefix("missing"),
              x.removesuffix("missing"), x.join(("a", "b"))):
    assert value.__class__ is str
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, str_subclass_conversion_callbacks)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class S(str):
    def __str__(self): return "override"
x = S("raw")
assert str(x) == "override"
assert str.__new__(str, x) == "override"
assert S(x) == "override"
assert str.__str__(x) == "raw"
assert str.__str__(x).__class__ is str
class Plain(str): pass
text = Plain("callback result")
class Source:
    def __str__(self): return text
assert str(Source()) is text
converted = S(Source())
assert converted.__class__ is S
assert converted == text
assert converted is not text
class Bad:
    def __str__(self): return 42
class Raises:
    def __str__(self): raise ValueError("conversion failed")
for cls in (str, Plain):
    try: cls(Bad())
    except TypeError as e: assert str(e) == "__str__ returned non-str"
    else: raise AssertionError
    try: cls(Raises())
    except ValueError as e: assert str(e) == "conversion failed"
    else: raise AssertionError
class Reentrant:
    def __str__(self):
        nested = Plain(123)
        nested.extra = "kept alive"
        return nested
assert Plain(Reentrant()) == "123"
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, str_new_errors_invalidation_and_combined_boundary)
{
    expect_python_error(L"str.__new__(int, 4)\n", L"TypeError",
                        L"str.__new__(int): int is not a subtype of str");
    expect_python_error(L"str.__new__(1, 4)\n", L"TypeError",
                        L"str.__new__(X): X is not a type object (int)");
    expect_python_error(L"str.__new__(None, 4)\n", L"TypeError",
                        L"str.__new__(X): X is not a type object (NoneType)");
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class S(str): pass
class T(S): pass
assert T(7).__class__ is T
def replacement(cls, value="replacement"): return value
S.__new__ = replacement
assert T() == "replacement"
assert T(11) == 11
del S.__new__
assert T(13).__class__ is T
assert T(13) == "13"
class Combined(str):
    def __init__(self): raise AssertionError
for mode in range(4):
    try:
        if mode == 0: S(1, 2)
        elif mode == 1: S(obj="text")
        elif mode == 2: S(b"text", "utf-8")
        else: Combined()
    except TypeError: pass
    else: raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, float_subclass_numeric_construction_and_attributes)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class F(float): pass
class G(F): pass
class I(int): pass
for cls in (float, F, G):
    assert cls().__class__ is cls
    assert cls() == 0.0
    for value in (0, 1, -1, True, False, 2.5, -0.0, 2**100, I(7), I(2**100)):
        x = cls(value)
        assert x.__class__ is cls
        assert x == float(value)
        if cls is not float:
            x.extra = 42
            assert x.__dict__["extra"] == 42
            y = cls(x)
            assert y.__class__ is cls
            assert y is not x
            assert not hasattr(y, "extra")
        direct = float.__new__(cls, value)
        assert direct.__class__ is cls
        assert direct == x
original = 2.5
assert float(original) is original
x = F(original)
assert float(x).__class__ is float
assert float(x) is not x
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, float_subclass_special_values_and_exact_results)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
import math
class F(float): pass
for value in (0.0, -0.0, 2.5, -2.5, math.inf, -math.inf, math.nan):
    x = F(value)
    assert x.__class__ is F
    assert math.copysign(1.0, x) == math.copysign(1.0, value)
    if math.isnan(value):
        assert math.isnan(x)
        assert math.isnan(float(x))
    else:
        assert float(x) == value
    for result in (float(x), +x, -x, abs(x), x + 0.0, x - 0.0, x * 1.0, x / 1.0):
        assert result.__class__ is float
        assert result is not x
for value in (2.5, F(2.5), 2.5, F(2.5)):
    assert (+value).__class__ is float
    assert abs(value).__class__ is float
class Override(F):
    def __pos__(self): return "positive override"
    def __abs__(self): return "abs override"
assert +Override(1) == "positive override"
assert abs(Override(1)) == "abs override"
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, float_new_class_errors_and_numeric_scope)
{
    expect_python_error(L"float.__new__(int, 4)\n", L"TypeError",
                        L"float.__new__(int): int is not a subtype of float");
    expect_python_error(L"float.__new__(1, 4)\n", L"TypeError",
                        L"float.__new__(X): X is not a type object (int)");
    expect_python_error(L"float.__new__(None, 4)\n", L"TypeError",
                        L"float.__new__(X): X is not a type object (NoneType)");
    expect_python_error(
        L"class F(float): pass\nF('2.5')\n", L"TypeError",
        L"float conversion is only implemented for float, int and bool");
    expect_python_error(L"class F(float): pass\nF(2**10000)\n",
                        L"OverflowError", L"int too large to convert to float");
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class F(float): pass
for mode in range(3):
    try:
        if mode == 0: F(1, 2)
        elif mode == 1: F(input=1)
        else: float(None)
    except TypeError:
        pass
    else:
        raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, float_new_invalidation_and_combined_boundary)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class F(float): pass
class G(F): pass
assert G(7).__class__ is G
def replacement(cls, value=9): return value
F.__new__ = replacement
assert G() == 9
assert G(11) == 11
del F.__new__
value = G(13)
assert value.__class__ is G
assert value == 13.0
class Combined(float):
    def __init__(self): raise AssertionError
try:
    Combined()
except TypeError:
    pass
else:
    raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, native_layout_new_only_bypasses_default_allocator_selection)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class C(float):
    def __new__(cls, value=7):
        return value
class D(C): pass
assert C() == 7
assert D(value=9) == 9
def replacement(cls):
    return 11
C.__new__ = replacement
assert D() == 11
del C.__new__
value = D()
assert value.__class__ is D
assert value == 0.0
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Interpreter, native_subclass_storage_rejects_generic_allocation)
{
    expect_python_error(L"class C(tuple): pass\nC()\n", L"TypeError",
                        L"native instance layout requires a builtin allocator");
    expect_python_error(L"class C(list, dict): pass\n", L"TypeError",
                        L"incompatible native instance layouts");
}

TEST(Interpreter, class_assignment_rejects_builtin_and_class_object_receivers)
{
    expect_python_error(L"class F:\n"
                        L"    pass\n"
                        L"a = object()\n"
                        L"a.__class__ = F\n",
                        L"TypeError",
                        L"__class__ assignment only supported for mutable "
                        L"types or ModuleType subclasses");
    expect_python_error(L"a = []\n"
                        L"a.__class__ = list\n",
                        L"TypeError",
                        L"__class__ assignment only supported for mutable "
                        L"types or ModuleType subclasses");
    expect_python_error(L"class F:\n"
                        L"    pass\n"
                        L"F.__class__ = type\n",
                        L"TypeError",
                        L"__class__ assignment only supported for mutable "
                        L"types or ModuleType subclasses");
}

TEST(Interpreter, class_assignment_rejects_instance_module_category_switch)
{
    expect_python_error(L"class F:\n"
                        L"    pass\n"
                        L"f = F()\n"
                        L"f.__class__ = __builtins__.__class__\n",
                        L"TypeError",
                        L"__class__ assignment only supported for mutable "
                        L"types or ModuleType subclasses");
}

TEST(Interpreter, class_assignment_rejects_module_instance_category_switch)
{
    expect_python_error(L"class F:\n"
                        L"    pass\n"
                        L"m = __builtins__\n"
                        L"m.__class__ = F\n",
                        L"TypeError",
                        L"__class__ assignment only supported for mutable "
                        L"types or ModuleType subclasses");
}

TEST(Interpreter, builtin_container_attribute_mappings_preserve_permissions)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
xs = [1]
d = {'x': 7}
ys = (2,)
for obj in (xs, d, ys):
    m = obj.__dict__
    assert len(m) == 0
    try:
        m['x'] = 9
    except TypeError:
        pass
    else:
        raise AssertionError
    try:
        del m['x']
    except TypeError:
        pass
    else:
        raise AssertionError
    try:
        m['__class__']
    except KeyError:
        pass
    else:
        raise AssertionError
assert xs[0] == 1
assert d['x'] == 7
assert ys[0] == 2
)");
    ASSERT_FALSE(result.is_exception_marker())
        << cl_test_string_to_wstring(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(Interpreter,
     native_subclass_attribute_mappings_are_live_and_isolate_payload)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    CodeObject *classes = context.compile_file(LR"(
class L(list): pass
class T(tuple): pass
class D(dict): pass
class F(float): pass
class S(str): pass
class E(Exception): pass
)");
    ASSERT_FALSE(context.thread()
                     ->run_clovervm_code_object(classes)
                     .is_exception_marker());
    auto cls = [&](const wchar_t *name) {
        return load_global_from_module_for_test(classes, interned_string(name))
            .get_ptr<ClassObject>();
    };
    Owned<TValue<List>> xs(
        context.thread()->make_internal_value<List>(cls(L"L")));
    xs.extract()->append(Value::from_smi(7));
    Owned<TValue<Tuple>> ys(
        context.thread()->make_internal_value<Tuple>(cls(L"T"), 1));
    ys.extract()->initialize_item_unchecked(0, Value::from_smi(8));
    Owned<TValue<Dict>> ds(
        context.thread()->make_internal_value<Dict>(cls(L"D")));
    ASSERT_FALSE(ds.extract()
                     ->set_item(context.thread(),
                                interned_string(L"first").raw_value(),
                                Value::from_smi(99))
                     .has_exception());
    Owned<TValue<Float>> fs(
        context.thread()->make_internal_value<Float>(cls(L"F"), 1.5));
    Owned<TValue<String>> ss(context.thread()->make_internal_value<String>(
        cls(L"S"), std::wstring(L"payload")));
    Owned<TValue<Exception>> e(make_exception_object_with_arg(
        context.thread(), TValue<ClassObject>::from_oop(cls(L"E")),
        Value::from_smi(19)));
    CodeObject *checks = context.compile_file(LR"(
class C: pass
c = C()
for obj in (xs, ys, ds, fs, ss, c):
    m = obj.__dict__
    assert len(m) == 0
    m['first'] = 1
    assert obj.first == 1
    obj.second = 2
    assert m['second'] == 2
    assert len(m) == 2
    del m['first']
    try:
        obj.first
    except AttributeError:
        pass
    else:
        raise AssertionError
    del obj.second
    assert len(m) == 0
    m['first'] = 3
    assert obj.first == 3
    assert obj.__dict__['first'] == 3
    assert str(m) == "{'first': 3}"
assert xs[0] == 7
assert ys[0] == 8
assert ds['first'] == 99
assert fs + 0.5 == 2.0
assert ss == 'payload'
m = e.__dict__
assert m['args'] is e.args
assert len(m['args']) == 1
assert m['args'][0] == 19
e.extra = 4
assert m['extra'] == 4
for operation in (0, 1):
    try:
        if operation == 0:
            m['args'] = (20,)
        else:
            del m['args']
    except TypeError:
        pass
    else:
        raise AssertionError
assert len(e.args) == 1
assert e.args[0] == 19
)");
    store_global_to_module_for_test(context, checks, L"xs", xs.value());
    store_global_to_module_for_test(context, checks, L"ys", ys.value());
    store_global_to_module_for_test(context, checks, L"ds", ds.value());
    store_global_to_module_for_test(context, checks, L"fs", fs.value());
    store_global_to_module_for_test(context, checks, L"ss", ss.value());
    store_global_to_module_for_test(context, checks, L"e", e.value());
    ASSERT_FALSE(context.thread()
                     ->run_clovervm_code_object(checks)
                     .is_exception_marker())
        << cl_test_string_to_wstring(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(Interpreter, builtin_type_classes_are_vm_roots_and_builtins)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    struct ExpectedBuiltinType
    {
        NativeLayoutId native_layout_id;
        const wchar_t *name;
    };

    constexpr ExpectedBuiltinType expected_types[] = {
        {NativeLayoutId::BigInt, L"int"},
        {NativeLayoutId::ClassObject, L"type"},
        {NativeLayoutId::String, L"str"},
        {NativeLayoutId::List, L"list"},
        {NativeLayoutId::Tuple, L"tuple"},
        {NativeLayoutId::Dict, L"dict"},
        {NativeLayoutId::SlotDict, L"slotdict"},
        {NativeLayoutId::Float, L"float"},
        {NativeLayoutId::Function, L"function"},
        {NativeLayoutId::CodeObject, L"code"},
        {NativeLayoutId::RangeIterator, L"range_iterator"},
        {NativeLayoutId::TupleIterator, L"tuple_iterator"},
        {NativeLayoutId::ListIterator, L"list_iterator"},
        {NativeLayoutId::Instance, L"object"},
    };

    ModuleObject *builtins =
        test_context.vm().global_builtins_module().extract();
    ClassObject *type_class = test_context.vm().type_class();
    ASSERT_NE(nullptr, type_class);

    for(const ExpectedBuiltinType &expected: expected_types)
    {
        ClassObject *cls = test_context.vm().class_for_native_layout(
            expected.native_layout_id);
        ASSERT_NE(nullptr, cls);
        EXPECT_EQ(type_class, cls->get_shape()->get_class());
        EXPECT_EQ(-1, cls->refcount);
        EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsClassObject));
        EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsImmutable));
        EXPECT_EQ(Value::not_present(),
                  cls->get_own_property(
                      test_context.vm().get_or_create_interned_string_value(
                          L"__class__")));

        TValue<String> name =
            test_context.vm().get_or_create_interned_string_value(
                expected.name);
        EXPECT_EQ(name, cls->get_name());
        EXPECT_EQ(test_context.vm().str_class(),
                  name.extract()->get_shape()->get_class());
        EXPECT_EQ(test_context.vm().str_instance_root_shape(),
                  name.extract()
                      ->get_shape()
                      ->get_class()
                      ->get_instance_root_shape());

        TValue<String> dunder_bases_name =
            test_context.vm().get_or_create_interned_string_value(L"__bases__");
        TValue<String> dunder_mro_name =
            test_context.vm().get_or_create_interned_string_value(L"__mro__");
        Value bases_value = cls->get_own_property(dunder_bases_name);
        Value mro_value = cls->get_own_property(dunder_mro_name);
        ASSERT_TRUE(can_convert_to<Tuple>(bases_value));
        ASSERT_TRUE(can_convert_to<Tuple>(mro_value));
        EXPECT_EQ(test_context.vm().tuple_class(),
                  bases_value.get_ptr<Object>()->get_shape()->get_class());
        EXPECT_EQ(test_context.vm().tuple_class(),
                  mro_value.get_ptr<Object>()->get_shape()->get_class());
        Tuple *bases = bases_value.get_ptr<Tuple>();
        Tuple *mro = mro_value.get_ptr<Tuple>();
        if(expected.native_layout_id == NativeLayoutId::Instance)
        {
            EXPECT_EQ(0u, bases->size());
            ASSERT_EQ(1u, mro->size());
            EXPECT_EQ(Value::from_oop(cls), mro->item_unchecked(0));
        }
        else
        {
            ASSERT_EQ(1u, bases->size());
            EXPECT_EQ(Value::from_oop(test_context.vm().object_class()),
                      bases->item_unchecked(0));
            ASSERT_EQ(2u, mro->size());
            EXPECT_EQ(Value::from_oop(cls), mro->item_unchecked(0));
            EXPECT_EQ(Value::from_oop(test_context.vm().object_class()),
                      mro->item_unchecked(1));
        }

        if(expected.native_layout_id == NativeLayoutId::CodeObject ||
           expected.native_layout_id == NativeLayoutId::Function ||
           expected.native_layout_id == NativeLayoutId::SlotDict ||
           expected.native_layout_id == NativeLayoutId::RangeIterator ||
           expected.native_layout_id == NativeLayoutId::TupleIterator ||
           expected.native_layout_id == NativeLayoutId::ListIterator)
        {
            EXPECT_EQ(Value::not_present(), builtins->get_own_property(name));
        }
        else
        {
            EXPECT_EQ(Value::from_oop(cls), builtins->get_own_property(name));
        }
    }

    TValue<String> post_bootstrap_name =
        test_context.vm().get_or_create_interned_string_value(
            L"post_bootstrap_name");
    EXPECT_EQ(test_context.vm().str_class(),
              post_bootstrap_name.extract()->get_shape()->get_class());
    EXPECT_EQ(test_context.vm().str_instance_root_shape(),
              post_bootstrap_name.extract()
                  ->get_shape()
                  ->get_class()
                  ->get_instance_root_shape());

    EXPECT_EQ(Value::not_present(),
              type_class->get_own_property(
                  test_context.vm().get_or_create_interned_string_value(
                      L"__class__")));

    ClassObject *str_class = test_context.vm().str_class();
    TValue<String> dunder_str_name =
        test_context.vm().get_or_create_interned_string_value(L"__str__");
    TValue<String> dunder_add_name =
        test_context.vm().get_or_create_interned_string_value(L"__add__");
    TValue<String> dunder_doc_name =
        test_context.vm().get_or_create_interned_string_value(L"__doc__");
    Value str_method = str_class->get_own_property(dunder_str_name);
    Value add_method = str_class->get_own_property(dunder_add_name);
    ASSERT_TRUE(can_convert_to<Function>(str_method));
    ASSERT_TRUE(can_convert_to<Function>(add_method));
    EXPECT_EQ(-1, str_method.get_ptr<Object>()->refcount);
    EXPECT_EQ(-1, add_method.get_ptr<Object>()->refcount);
    Optional<TValue<String>> str_docstring =
        assume_convert_to<Function>(str_method)->docstring.value();
    Optional<TValue<String>> add_docstring =
        assume_convert_to<Function>(add_method)->docstring.value();
    ASSERT_TRUE(str_docstring.has_value());
    ASSERT_TRUE(add_docstring.has_value());
    EXPECT_EQ(test_context.vm().get_or_create_interned_string_value(
                  L"Return str(self)."),
              str_docstring.value());
    EXPECT_EQ(test_context.vm().get_or_create_interned_string_value(
                  L"Return self + value."),
              add_docstring.value());
    EXPECT_EQ(str_docstring.value().raw_value(),
              load_attr(str_method, dunder_doc_name));
    EXPECT_EQ(add_docstring.value().raw_value(),
              load_attr(add_method, dunder_doc_name));
    EXPECT_FALSE(
        str_class->set_own_property(dunder_str_name, Value::from_smi(99)));
}

TEST(Interpreter, float_objects_have_builtin_class_and_string_methods)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    Value value =
        test_context.thread()->make_object_value<Float>(1.5).raw_value();
    ASSERT_TRUE(can_convert_to<Float>(value));
    EXPECT_EQ(NativeLayoutId::Float,
              value.get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(test_context.vm().float_class(),
              value.get_ptr<Object>()->get_shape()->get_class());

    TValue<String> dunder_str_name =
        test_context.vm().get_or_create_interned_string_value(L"__str__");
    TValue<String> dunder_repr_name =
        test_context.vm().get_or_create_interned_string_value(L"__repr__");
    TValue<String> dunder_eq_name =
        test_context.vm().get_or_create_interned_string_value(L"__eq__");
    TValue<String> dunder_ne_name =
        test_context.vm().get_or_create_interned_string_value(L"__ne__");
    TValue<String> dunder_add_name =
        test_context.vm().get_or_create_interned_string_value(L"__add__");
    TValue<String> dunder_radd_name =
        test_context.vm().get_or_create_interned_string_value(L"__radd__");
    TValue<String> dunder_sub_name =
        test_context.vm().get_or_create_interned_string_value(L"__sub__");
    TValue<String> dunder_rsub_name =
        test_context.vm().get_or_create_interned_string_value(L"__rsub__");
    TValue<String> dunder_mul_name =
        test_context.vm().get_or_create_interned_string_value(L"__mul__");
    TValue<String> dunder_rmul_name =
        test_context.vm().get_or_create_interned_string_value(L"__rmul__");
    TValue<String> dunder_truediv_name =
        test_context.vm().get_or_create_interned_string_value(L"__truediv__");
    TValue<String> dunder_rtruediv_name =
        test_context.vm().get_or_create_interned_string_value(L"__rtruediv__");
    TValue<String> dunder_floordiv_name =
        test_context.vm().get_or_create_interned_string_value(L"__floordiv__");
    TValue<String> dunder_rfloordiv_name =
        test_context.vm().get_or_create_interned_string_value(L"__rfloordiv__");
    TValue<String> dunder_mod_name =
        test_context.vm().get_or_create_interned_string_value(L"__mod__");
    TValue<String> dunder_rmod_name =
        test_context.vm().get_or_create_interned_string_value(L"__rmod__");
    TValue<String> dunder_lt_name =
        test_context.vm().get_or_create_interned_string_value(L"__lt__");
    TValue<String> dunder_le_name =
        test_context.vm().get_or_create_interned_string_value(L"__le__");
    TValue<String> dunder_gt_name =
        test_context.vm().get_or_create_interned_string_value(L"__gt__");
    TValue<String> dunder_ge_name =
        test_context.vm().get_or_create_interned_string_value(L"__ge__");

    Value str_result =
        test_context.thread()->call_clovervm_method(value, dunder_str_name);
    ASSERT_TRUE(can_convert_to<String>(str_result));
    EXPECT_TRUE(string_eq(
        TValue<String>::from_value_unchecked(str_result),
        test_context.vm().get_or_create_interned_string_value(L"1.5")));

    Value repr_result =
        test_context.thread()->call_clovervm_method(value, dunder_repr_name);
    ASSERT_TRUE(can_convert_to<String>(repr_result));
    EXPECT_TRUE(string_eq(
        TValue<String>::from_value_unchecked(repr_result),
        test_context.vm().get_or_create_interned_string_value(L"1.5")));

    auto expect_float_method_result = [&](Value receiver, TValue<String> name,
                                          Value argument, double expected) {
        Value result = test_context.thread()->call_clovervm_method(
            receiver, name, argument);
        ASSERT_TRUE(can_convert_to<Float>(result));
        EXPECT_DOUBLE_EQ(expected, result.get_ptr<Float>()->value());
    };

    Value equal_float = test_context.thread()->call_clovervm_method(
        value, dunder_eq_name,
        test_context.thread()->make_object_value<Float>(1.5).raw_value());
    EXPECT_EQ(Value::True(), equal_float);
    Value one_value =
        test_context.thread()->make_object_value<Float>(1.0).raw_value();
    EXPECT_EQ(Value::True(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_eq_name, Value::from_smi(1)));
    EXPECT_EQ(Value::True(), test_context.thread()->call_clovervm_method(
                                 one_value, dunder_eq_name, Value::True()));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(value, dunder_eq_name,
                                                          Value::None()));
    EXPECT_EQ(Value::False(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_ne_name, Value::from_smi(1)));
    EXPECT_EQ(Value::True(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_ne_name, Value::from_smi(2)));
    EXPECT_EQ(Value::False(), test_context.thread()->call_clovervm_method(
                                  one_value, dunder_ne_name, Value::True()));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(value, dunder_ne_name,
                                                          Value::None()));
    expect_float_method_result(value, dunder_add_name, Value::from_smi(2), 3.5);
    expect_float_method_result(value, dunder_add_name, Value::True(), 2.5);
    expect_float_method_result(value, dunder_radd_name, Value::from_smi(2),
                               3.5);
    expect_float_method_result(value, dunder_sub_name, Value::from_smi(2),
                               -0.5);
    expect_float_method_result(value, dunder_rsub_name, Value::from_smi(2),
                               0.5);
    expect_float_method_result(value, dunder_mul_name, Value::from_smi(2), 3.0);
    expect_float_method_result(value, dunder_rmul_name, Value::from_smi(2),
                               3.0);
    expect_float_method_result(value, dunder_truediv_name, Value::from_smi(2),
                               0.75);
    expect_float_method_result(value, dunder_rtruediv_name, Value::from_smi(3),
                               2.0);
    expect_float_method_result(value, dunder_floordiv_name, Value::from_smi(1),
                               1.0);
    expect_float_method_result(value, dunder_rfloordiv_name, Value::from_smi(5),
                               3.0);
    expect_float_method_result(value, dunder_mod_name, Value::from_smi(1), 0.5);
    expect_float_method_result(value, dunder_rmod_name, Value::from_smi(5),
                               0.5);
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(
                  value, dunder_add_name, Value::None()));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(
                  value, dunder_truediv_name, Value::None()));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(
                  value, dunder_floordiv_name, Value::None()));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(
                  value, dunder_mod_name, Value::None()));
    EXPECT_EQ(Value::True(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_lt_name, Value::from_smi(2)));
    EXPECT_EQ(Value::False(), test_context.thread()->call_clovervm_method(
                                  one_value, dunder_lt_name, Value::True()));
    EXPECT_EQ(Value::True(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_le_name, Value::from_smi(1)));
    EXPECT_EQ(Value::False(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_le_name, Value::from_smi(0)));
    EXPECT_EQ(Value::True(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_gt_name, Value::from_smi(0)));
    EXPECT_EQ(Value::False(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_gt_name, Value::from_smi(1)));
    EXPECT_EQ(Value::True(), test_context.thread()->call_clovervm_method(
                                 one_value, dunder_ge_name, Value::True()));
    EXPECT_EQ(Value::False(),
              test_context.thread()->call_clovervm_method(
                  one_value, dunder_ge_name, Value::from_smi(2)));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(value, dunder_lt_name,
                                                          Value::None()));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.thread()->call_clovervm_method(value, dunder_ge_name,
                                                          Value::None()));
}

TEST(Interpreter, float_string_methods_format_special_values)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> dunder_str_name =
        test_context.vm().get_or_create_interned_string_value(L"__str__");
    TValue<String> dunder_repr_name =
        test_context.vm().get_or_create_interned_string_value(L"__repr__");

    auto expect_method_result = [&](double value, TValue<String> method_name,
                                    const wchar_t *expected) {
        Value float_value =
            test_context.thread()->make_object_value<Float>(value).raw_value();
        Value result = test_context.thread()->call_clovervm_method(float_value,
                                                                   method_name);
        ASSERT_TRUE(can_convert_to<String>(result));
        EXPECT_STREQ(expected, string_as_wchar_t(
                                   TValue<String>::from_value_assumed(result)));
    };

    expect_method_result(-0.0, dunder_str_name, L"-0.0");
    expect_method_result(std::numeric_limits<double>::infinity(),
                         dunder_str_name, L"inf");
    expect_method_result(-std::numeric_limits<double>::infinity(),
                         dunder_str_name, L"-inf");
    expect_method_result(std::numeric_limits<double>::quiet_NaN(),
                         dunder_repr_name, L"nan");
}

static Value run_exhausted_for_iter(test::VmTestContext &test_context,
                                    Bytecode opcode)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<exhausted-for-iter-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    JumpTarget exhausted_target(&builder);

    if(opcode == Bytecode::ForIter)
    {
        CodeObjectBuilder::TemporaryReg iterator_reg(builder);
        RangeIterator *iterator =
            test_context.thread()->make_object_raw<RangeIterator>(
                TValue<SMI>::from_smi(0), TValue<SMI>::from_smi(0),
                TValue<SMI>::from_smi(1));
        uint8_t iterator_idx = uint8_t(
            builder.allocate_constant(Value::from_oop(iterator)).value());
        builder.emit_lda_constant(0, iterator_idx).value();
        builder.emit_star(0, iterator_reg).value();
        builder.emit_lda_smi(0, 42).value();
        builder.emit_for_iter(0, iterator_reg, exhausted_target).value();
    }
    else
    {
        assert(opcode == Bytecode::ForIterRange1 ||
               opcode == Bytecode::ForIterRangeStep);
        uint32_t n_range_regs = opcode == Bytecode::ForIterRange1 ? 2u : 3u;
        CodeObjectBuilder::TemporaryReg range_regs(builder, n_range_regs);
        builder.emit_lda_smi(0, 0).value();
        builder.emit_star(0, range_regs).value();
        builder.emit_lda_smi(0, 0).value();
        builder.emit_star(0, range_regs + 1).value();
        if(opcode == Bytecode::ForIterRangeStep)
        {
            builder.emit_lda_smi(0, 1).value();
            builder.emit_star(0, range_regs + 2).value();
        }
        builder.emit_lda_smi(0, 42).value();
        builder.emit_for_iter_range(0, opcode, range_regs, exhausted_target)
            .value();
    }

    exhausted_target.resolve().value();
    builder.emit_return(0).value();
    return test_context.thread()->run_clovervm_code_object(
        builder.finalize().value());
}

TEST(Interpreter, exhausted_for_iter_opcodes_set_accumulator_to_none)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    EXPECT_EQ(Value::None(),
              run_exhausted_for_iter(test_context, Bytecode::ForIter));
    EXPECT_EQ(Value::None(),
              run_exhausted_for_iter(test_context, Bytecode::ForIterRange1));
    EXPECT_EQ(Value::None(),
              run_exhausted_for_iter(test_context, Bytecode::ForIterRangeStep));
}

TEST(Interpreter, direct_range_for_loop_reports_integer_argument_errors)
{
    expect_python_error(L"for x in range('bad'):\n"
                        L"    0\n",
                        L"TypeError", L"range() arguments must be integers");
}

TEST(Interpreter, direct_range_for_loop_reports_zero_step_errors)
{
    expect_python_error(L"for x in range(1, 4, 0):\n"
                        L"    0\n",
                        L"ValueError", L"range() arg 3 must not be zero");
}

TEST(Interpreter, global_delete_does_not_delete_builtin_fallback)
{
    expect_python_error(L"range\n"
                        L"del range\n",
                        L"NameError", L"name 'range' is not defined");
}

TEST(Interpreter, range_builtin_requires_integer_argument)
{
    expect_python_error(L"range('bad')\n", L"TypeError",
                        L"range() arguments must be integers");
    expect_python_error(L"range(1, 'bad')\n", L"TypeError",
                        L"range() arguments must be integers");
    expect_python_error(L"range(1, 2, 'bad')\n", L"TypeError",
                        L"range() arguments must be integers");
}

TEST(Interpreter, range_integer_argument_error_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    range('bad')\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError", L"range() arguments must be integers");
}

TEST(Interpreter, range_builtin_rejects_zero_step)
{
    expect_python_error(L"range(1, 2, 0)\n", L"ValueError",
                        L"range() arg 3 must not be zero");
}

TEST(Interpreter, range_zero_step_error_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    range(1, 2, 0)\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"ValueError", L"range() arg 3 must not be zero");
}

TEST(Interpreter, range_builtin_rejects_wrong_arity_at_function_boundary)
{
    expect_python_error(L"range()\n", L"TypeError",
                        L"wrong number of arguments");
    expect_python_error(L"range(1, 2, 3, 4)\n", L"TypeError",
                        L"wrong number of arguments");
}

TEST(Interpreter, python_defined_next_builtin_rejects_multiple_defaults)
{
    expect_python_error(L"next(iter(()), 42, 43)\n", L"TypeError", L"");
}

TEST(Interpreter, list_iterator_stays_exhausted_after_append)
{
    test::VmTestContext test_context;
    Value result =
        test_context.run_file(L"items = []\n"
                              L"iterator = iter(items)\n"
                              L"assert next(iterator, None) is None\n"
                              L"items.append(1)\n"
                              L"assert next(iterator, None) is None\n"
                              L"assert next(iterator, None) is None\n"
                              L"items = [1]\n"
                              L"iterator = iter(items)\n"
                              L"assert next(iterator) == 1\n"
                              L"items.append(2)\n"
                              L"assert next(iterator) == 2\n"
                              L"assert next(iterator, None) is None\n"
                              L"items.append(3)\n"
                              L"assert next(iterator, None) is None\n"
                              L"assert iter(iterator) is iterator\n"
                              L"for item in iterator:\n"
                              L"    assert False\n"
                              L"None\n");
    EXPECT_EQ(Value::None(), result);
}

TEST(Interpreter, list_iterator_stays_exhausted_after_shrink_and_regrow)
{
    test::VmTestContext test_context;
    Value result =
        test_context.run_file(L"items = [1, 2]\n"
                              L"iterator = iter(items)\n"
                              L"assert next(iterator) == 1\n"
                              L"items.clear()\n"
                              L"assert next(iterator, None) is None\n"
                              L"items.append(3)\n"
                              L"items.append(4)\n"
                              L"assert next(iterator, None) is None\n"
                              L"try:\n"
                              L"    iterator.__next__()\n"
                              L"except StopIteration:\n"
                              L"    pass\n"
                              L"else:\n"
                              L"    assert False\n"
                              L"None\n");
    EXPECT_EQ(Value::None(), result);
}

TEST(Interpreter, python_defined_len_builtin_missing_method_error)
{
    expect_python_error(L"len(1)\n", L"TypeError", L"object has no len()");
}

TEST(Interpreter, python_defined_hash_builtin_matches_runtime_string_hash)
{
    test::VmTestContext test_context;

    TValue<String> abc =
        test_context.thread()->make_object_value<String>(L"abc");
    EXPECT_EQ(string_hash_normalized(abc).raw_value(),
              test_context.run_file(L"hash('abc')\n"));
}

TEST(Interpreter, python_defined_hash_builtin_rejects_non_integer_hash_result)
{
    expect_python_error(L"class C:\n"
                        L"    def __hash__(self):\n"
                        L"        return 'hello'\n"
                        L"hash(C())\n",
                        L"TypeError",
                        L"__hash__ method should return an integer");
}

TEST(Interpreter, python_defined_hash_builtin_propagates_dunder_hash_exception)
{
    expect_python_error(L"class C:\n"
                        L"    def __hash__(self):\n"
                        L"        raise ValueError\n"
                        L"hash(C())\n",
                        L"ValueError", L"");
}

TEST(Interpreter, python_defined_hash_builtin_missing_method_error)
{
    expect_python_error(L"class C:\n"
                        L"    pass\n"
                        L"hash(C())\n",
                        L"TypeError", L"object is unhashable");
}

TEST(Interpreter, python_defined_print_builtin_writes_values_to_stdout)
{
    CapturedStdoutRun run =
        run_file_with_captured_stdout(L"print(1, True, None, \"ok\")\n");

    EXPECT_EQ(Value::None(), run.return_value);
    EXPECT_EQ(L"1 True None ok\n", run.stdout_text);
}

TEST(Interpreter, python_defined_print_builtin_formats_float_literals)
{
    CapturedStdoutRun run =
        run_file_with_captured_stdout(L"print(1.5, 1.0, 1e20)\n");

    EXPECT_EQ(Value::None(), run.return_value);
    EXPECT_EQ(L"1.5 1.0 1e+20\n", run.stdout_text);
}

TEST(Interpreter, python_defined_print_builtin_writes_blank_line_for_no_args)
{
    CapturedStdoutRun run = run_file_with_captured_stdout(L"print()\n");

    EXPECT_EQ(Value::None(), run.return_value);
    EXPECT_EQ(L"\n", run.stdout_text);
}

TEST(Interpreter, python_defined_print_builtin_accepts_keyword_formatting)
{
    CapturedStdoutRun run =
        run_file_with_captured_stdout(L"print(1, 2, 3, sep=', ', end='!')\n");

    EXPECT_EQ(Value::None(), run.return_value);
    EXPECT_EQ(L"1, 2, 3!", run.stdout_text);
}

TEST(Interpreter, python_defined_print_builtin_treats_none_sep_end_as_defaults)
{
    CapturedStdoutRun run =
        run_file_with_captured_stdout(L"print(1, 2, sep=None, end=None)\n");

    EXPECT_EQ(Value::None(), run.return_value);
    EXPECT_EQ(L"1 2\n", run.stdout_text);
}

TEST(Interpreter, python_defined_print_builtin_rejects_non_string_sep_end)
{
    expect_python_error(L"print(1, sep=2)\n", L"TypeError", L"");
    expect_python_error(L"print(1, end=2)\n", L"TypeError", L"");
}

TEST(Interpreter, interactive_expression_returns_value)
{
    test::VmTestContext test_context;

    CodeObject *code_obj = test_context.thread()
                               ->compile(L"1 + 2\n", StartRule::Interactive)
                               .value();
    Value result = test_context.thread()->run_clovervm_code_object(code_obj);

    EXPECT_EQ(Value::from_smi(3), result);
}

TEST(Interpreter, interactive_assignment_returns_none_and_persists_scope)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope active_thread(test_context.thread());
    TValue<String> module_name =
        test_context.vm().get_or_create_interned_string_value(L"<interactive>");
    ModuleObject *module = test_context.make_test_module_object(
        module_name, test_context.vm().global_builtins_module().raw_value());

    CodeObject *assignment =
        test_context.thread()
            ->compile_in_module(L"x = 4\n", StartRule::Interactive, module,
                                LanguageMode::StandardsCompliant)
            .value();
    EXPECT_EQ(Value::None(),
              test_context.thread()->run_clovervm_code_object(assignment));

    CodeObject *expression =
        test_context.thread()
            ->compile_in_module(L"x + 1\n", StartRule::Interactive, module,
                                LanguageMode::StandardsCompliant)
            .value();
    EXPECT_EQ(Value::from_smi(5),
              test_context.thread()->run_clovervm_code_object(expression));
}

TEST(Interpreter, python_defined_print_builtin_propagates_str_errors)
{
    expect_python_error(L"class Bad:\n"
                        L"    def __str__(self):\n"
                        L"        return 1\n"
                        L"print(Bad())\n",
                        L"TypeError", L"__clover_write_stdout__ expects str");
}

TEST(Interpreter, python_defined_min_and_max_builtins_report_empty_input)
{
    expect_python_error(L"min()\n", L"TypeError", L"");
    expect_python_error(L"max()\n", L"TypeError", L"");
    expect_python_error(L"min(())\n", L"ValueError", L"");
    expect_python_error(L"max([])\n", L"ValueError", L"");
}

TEST(Interpreter, python_defined_iter_and_next_builtin_missing_method_errors)
{
    expect_python_error(L"iter(1)\n", L"TypeError", L"object is not iterable");
    expect_python_error(L"next(1)\n", L"TypeError",
                        L"object is not an iterator");
}

TEST(Interpreter, for_loop_rejects_non_iterable)
{
    expect_python_error(L"for x in 1:\n"
                        L"    x\n",
                        L"TypeError", L"object is not iterable");
}

TEST(Interpreter, for_loop_non_iterable_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    for x in 1:\n"
                        L"        x\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError", L"object is not iterable");
}

TEST(Interpreter, generic_for_loop_discards_stop_iteration_value)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"class Iterator:\n"
                                  L"    def __iter__(self):\n"
                                  L"        return self\n"
                                  L"    def __next__(self):\n"
                                  L"        native_stop()\n"
                                  L"result = 0\n"
                                  L"for x in Iterator():\n"
                                  L"    result = 99\n"
                                  L"else:\n"
                                  L"    result = 7\n"
                                  L"result\n");

    store_global_to_module_for_test(
        test_context, code_obj, L"native_stop",
        make_intrinsic_function(&test_context.vm(),
                                native_stop_iteration_with_value));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(7), actual);
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(Interpreter, generic_for_loop_propagates_non_stop_iteration)
{
    expect_python_error(L"class Iterator:\n"
                        L"    def __iter__(self):\n"
                        L"        return self\n"
                        L"    def __next__(self):\n"
                        L"        raise ValueError\n"
                        L"for x in Iterator():\n"
                        L"    x\n",
                        L"ValueError", L"");
}

TEST(Interpreter, with_statement_reraises_when_exit_returns_false)
{
    expect_python_error(L"class Manager:\n"
                        L"    def __enter__(self):\n"
                        L"        return self\n"
                        L"    def __exit__(self, typ, exc, tb):\n"
                        L"        return False\n"
                        L"with Manager():\n"
                        L"    raise ValueError\n",
                        L"ValueError", L"");
}

TEST(Interpreter, left_shift_negative_count)
{
    expect_python_error(L"a = 1\n"
                        L"b = -1\n"
                        L"a << b\n",
                        L"ValueError", L"negative shift count");
}

TEST(Interpreter, right_shift_negative_count)
{
    expect_python_error(L"a = 8\n"
                        L"b = -1\n"
                        L"a >> b\n",
                        L"ValueError", L"negative shift count");
}

TEST(Interpreter, negative_shift_count_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    1 << -1\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"ValueError", L"negative shift count");
}

TEST(Interpreter, shift_reports_unsupported_operands)
{
    expect_python_error(L"\"a\" << 1\n", L"TypeError",
                        L"unsupported operand type(s) for shift");
    expect_python_error(L"1 >> \"a\"\n", L"TypeError",
                        L"unsupported operand type(s) for shift");
}

TEST(Interpreter, negate_overflow)
{
    Value expected = Value::from_smi(kMinSmi);
    test::FileRunner file_runner(L"-288230376151711743 - 1\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);

    test::VmTestContext context;
    Value promoted = context.run_file(L"x = -288230376151711743 - 1\n"
                                      L"-x\n");
    ASSERT_TRUE(can_convert_to<BigInt>(promoted));
    expect_string_value(context.run_file(L"x = -288230376151711743 - 1\n"
                                         L"str(-x)\n"),
                        L"288230376151711744");
}

TEST(Interpreter, prohibited_builtin_subclass_bases)
{
    expect_python_error(L"class C(bool):\n    pass\n", L"TypeError",
                        L"type is not an acceptable base type");
    expect_python_error(L"class C(int, bool):\n    pass\n", L"TypeError",
                        L"type is not an acceptable base type");
    expect_python_error(L"class C(slice):\n    pass\n", L"TypeError",
                        L"type is not an acceptable base type");
}

TEST(Interpreter, builtin_implementations_share_an_unpublished_private_module)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    ModuleObject *public_module =
        context.vm().global_builtins_module().extract();
    Value len_function =
        public_module->get_own_property(interned_string(L"len"));
    ASSERT_TRUE(can_convert_to<Function>(len_function));
    ModuleObject *implementation = len_function.get_ptr<Function>()
                                       ->code_object.extract()
                                       ->get_defining_module()
                                       .extract();
    ASSERT_NE(public_module, implementation);
    Value fallback = implementation->get_builtins_binding();
    ASSERT_TRUE(can_convert_to<Dict>(fallback));
    EXPECT_EQ(0u, fallback.get_ptr<Dict>()->size());
    for(const wchar_t *name:
        {L"list", L"tuple", L"dict", L"int", L"str", L"isinstance",
         L"TypeError", L"ValueError", L"StopIteration", L"NotImplemented",
         L"len", L"iter", L"hasattr"})
    {
        EXPECT_EQ(public_module->get_own_property(interned_string(name)),
                  implementation->get_own_property(interned_string(name)));
    }
    for(ClassObject *cls:
        {context.vm().list_class(), context.vm().tuple_class(),
         context.vm().dict_class()})
    {
        const wchar_t *name =
            cls == context.vm().dict_class() ? L"update" : L"count";
        Value method = cls->get_own_property(interned_string(name));
        ASSERT_TRUE(can_convert_to<Function>(method));
        EXPECT_EQ(implementation, method.get_ptr<Function>()
                                      ->code_object.extract()
                                      ->get_defining_module()
                                      .extract());
    }
    EXPECT_EQ(implementation, context.vm()
                                  .membership_iter_fallback_function()
                                  .extract()
                                  ->code_object.extract()
                                  ->get_defining_module()
                                  .extract());
    for(const wchar_t *name:
        {L"_clover_builtin_getattr", L"_clover_builtin_getattr_default",
         L"_clover_builtin_hasattr", L"_clover_builtin_dir",
         L"_clover_dict_allocate", L"_clover_dict_next", L"_clover_dict_new",
         L"_clover_dict_update", L"_clover_list_eq", L"_clover_tuple_eq",
         L"_clover_iter_membership_fallback"})
    {
        EXPECT_TRUE(public_module->get_own_property(interned_string(name))
                        .is_not_present());
        EXPECT_FALSE(implementation->get_own_property(interned_string(name))
                         .is_not_present());
    }
    Value result =
        context.run_file(L"import sys\n"
                         L"assert '_clover_builtin_impl' not in sys.modules\n"
                         L"try:\n"
                         L"    import _clover_builtin_impl\n"
                         L"except ModuleNotFoundError:\n"
                         L"    pass\n"
                         L"else:\n"
                         L"    assert False\n"
                         L"marker = 81\n"
                         L"assert globals()['marker'] == 81\n"
                         L"assert locals()['marker'] == 81\n"
                         L"True\n");
    EXPECT_EQ(Value::True(), result);
}

TEST(Interpreter,
     private_builtin_globals_do_not_fall_back_after_a_warmed_seed_is_removed)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Value function =
        context.vm().global_builtins_module().extract()->get_own_property(
            interned_string(L"len"));
    ModuleObject *implementation = function.get_ptr<Function>()
                                       ->code_object.extract()
                                       ->get_defining_module()
                                       .extract();
    auto compiled = context.thread()->compile_in_module(
        L"list\n", StartRule::File, implementation,
        LanguageMode::StandardsCompliant);
    ASSERT_FALSE(compiled.has_exception());
    CodeObject *code = compiled.value();
    Value original_list = Value::from_oop(context.vm().list_class());
    EXPECT_EQ(original_list, context.thread()->run_clovervm_code_object(code));
    EXPECT_EQ(original_list, context.thread()->run_clovervm_code_object(code));
    ASSERT_TRUE(delete_module_global(implementation, interned_string(L"list")));
    Value result = context.thread()->run_clovervm_code_object(code);
    EXPECT_TRUE(result.is_exception_marker());
    ASSERT_TRUE(context.thread()->has_pending_exception());
    expect_thread_python_error(context.thread(), L"NameError",
                               L"name 'list' is not defined");
    context.thread()->clear_pending_exception();
    ASSERT_TRUE(implementation->set_own_property(interned_string(L"list"),
                                                 original_list));
    EXPECT_EQ(original_list, context.thread()->run_clovervm_code_object(code));
}

TEST(Interpreter,
     exported_python_builtins_keep_private_dependencies_during_callbacks)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Value result = context.run_file(LR"(
import builtins
Next = next
Len = len
Stop = StopIteration
class Iterator:
    def __next__(self):
        builtins.len = None
        raise Stop
iterator = Iterator()
# Warm both paths before a callback replaces the public dependency.
assert Next(iter([4])) == 4
assert Next(iter(()), 7) == 7
try:
    assert Next(iterator, 9) == 9
    assert Next(iter(()), 8) == 8
    assert Len((1, 2)) == 2
finally:
    builtins.len = Len
True
)");
    EXPECT_EQ(Value::True(), result);
}

TEST(Interpreter, conditional_expression_precedence_and_contexts)
{
    test::VmTestContext context;
    EXPECT_EQ(Value::True(), context.run_file(LR"(
assert (1 if True else 2) == 1
assert (1 if False else 2) == 2
assert (1 or 2 if False else 3) == 3
assert (1 if False else 2 if True else 3) == 2
assert ((1 if False else 2) if True else 3) == 2
assert (1 if (False if True else True) else 2) == 2
assert (1 if 0 else 2) == 2
assert (1 if None else 2) == 2
assert (1 if -3 else 2) == 1
assert (1 if 0.0 else 2) == 2
assert (1 if 1.5 else 2) == 1
def identity(value): return value
assert identity(1 if False else 2) == 2
values = [1 if True else 2, 3 if False else 4]
assert values[0] == 1
assert values[1] == 4
assert values[0 if True else 1] == 1
assert (10 + (1 if False else 2)) == 12
True
)"));
}

TEST(Interpreter, conditional_expression_is_lazy_and_preserves_identity)
{
    test::VmTestContext context;
    EXPECT_EQ(Value::True(), context.run_file(LR"(
trace = 0
def condition(value):
    global trace
    trace = trace * 10 + 1
    return value
def arm(value):
    global trace
    trace = trace * 10 + 2
    return value
def fail(): raise ValueError("unselected")
left = [1]
right = [2]
assert (arm(left) if condition(True) else fail()) is left
assert trace == 12
trace = 0
assert (fail() if condition(False) else arm(right)) is right
assert trace == 12
assert (left if True else missing) is left
assert (missing if False else right) is right
True
)"));
}

TEST(Interpreter, conditional_expression_propagates_exceptions)
{
    expect_python_error(L"def fail(): raise ValueError('condition')\n"
                        L"1 if fail() else 2\n",
                        L"ValueError", L"condition");
    expect_python_error(L"def fail(): raise ValueError('arm')\n"
                        L"fail() if True else 2\n",
                        L"ValueError", L"arm");
    expect_python_error(L"def fail(): raise ValueError('arm')\n"
                        L"1 if False else fail()\n",
                        L"ValueError", L"arm");
    expect_python_error(L"1 if 'unsupported' else 2\n", L"TypeError",
                        L"unsupported truthiness for object");
}

TEST(Interpreter, conditional_expression_analyzes_local_reads_in_both_arms)
{
    test::VmTestContext context;
    EXPECT_EQ(Value::True(), context.run_file(LR"(
def choose(flag):
    left = 1
    right = 2
    return left if flag else right
assert choose(True) == 1
assert choose(False) == 2
def skip_missing(flag):
    result = 1 if flag else missing
    missing = 2
    return result
assert skip_missing(True) == 1
True
)"));
    expect_python_error(L"def f(flag):\n"
                        L"    result = missing if flag else 2\n"
                        L"    missing = 1\n"
                        L"    return result\n"
                        L"f(True)\n",
                        L"NameError", L"name 'missing' is not defined");
    expect_python_error(L"def f(flag):\n"
                        L"    result = 1 if flag else missing\n"
                        L"    missing = 2\n"
                        L"    return result\n"
                        L"f(False)\n",
                        L"NameError", L"name 'missing' is not defined");
    expect_python_error(L"def f():\n"
                        L"    result = 1 if missing else 2\n"
                        L"    missing = True\n"
                        L"    return result\n"
                        L"f()\n",
                        L"NameError", L"name 'missing' is not defined");
}

TEST(Interpreter, range_accepts_bool_arguments_in_calls_and_direct_loops)
{
    test::VmTestContext context;
    EXPECT_EQ(Value::True(), context.run_file(LR"(
def collect(iterator):
    values = []
    for value in iterator: values.append(value)
    return values
assert collect(range(False)) == []
assert collect(range(True)) == [0]
assert collect(range(False, True)) == [0]
assert collect(range(False, 3, True)) == [0, 1, 2]
values = []
for value in range(False): values.append(value)
assert values == []
for value in range(True): values.append(value)
assert values == [0]
values = []
for value in range(False, True): values.append(value)
assert values == [0]
values = []
for value in range(False, 3, True): values.append(value)
assert values == [0, 1, 2]
True
)"));
}

TEST(Interpreter, range_accepts_small_heap_integer_arguments)
{
    test::VmTestContext context;
    EXPECT_EQ(Value::True(), context.run_file(LR"(
class I(int): pass
def collect(iterator):
    values = []
    for value in iterator: values.append(value)
    return values
assert collect(range(I(0))) == []
assert collect(range(I(4))) == [0, 1, 2, 3]
assert collect(range(I(-2), I(3))) == [-2, -1, 0, 1, 2]
assert collect(range(I(5), I(-2), I(-2))) == [5, 3, 1, -1]
values = []
for value in range(I(0)): values.append(value)
assert values == []
for value in range(I(4)): values.append(value)
assert values == [0, 1, 2, 3]
values = []
for value in range(I(-2), I(3)): values.append(value)
assert values == [-2, -1, 0, 1, 2]
values = []
for value in range(I(5), I(-2), I(-2)): values.append(value)
assert values == [5, 3, 1, -1]
for value in values: assert value.__class__ is int
True
)"));
}

TEST(Interpreter, range_normalized_zero_steps_raise_value_error)
{
    for(const wchar_t *step: {L"False", L"I(0)"})
    {
        std::wstring prefix = L"class I(int): pass\n";
        std::wstring args = L"1, 4, " + std::wstring(step);
        expect_python_error((prefix + L"range(" + args + L")\n").c_str(),
                            L"ValueError", L"range() arg 3 must not be zero");
        expect_python_error((prefix + L"for x in range(" + args +
                             L"):\n"
                             L"    raise AssertionError\n")
                                .c_str(),
                            L"ValueError", L"range() arg 3 must not be zero");
    }
}

TEST(Interpreter, range_conversion_errors_are_catchable_on_both_paths)
{
    for(const wchar_t *args: {L"'bad'", L"'bad', 3", L"0, 'bad'",
                              L"0, 3, 'bad'", L"I(0), 'bad', I(1)"})
    {
        std::wstring prefix = L"class I(int): pass\ndef fail():\n";
        std::wstring suffix =
            L"\ntry:\n    fail()\nexcept TypeError as e:\n"
            L"    assert str(e) == 'range() arguments must be integers'\n"
            L"else: raise AssertionError\nTrue\n";
        for(bool direct_loop: {false, true})
        {
            std::wstring body =
                direct_loop ? L"    for value in range(" + std::wstring(args) +
                                  L"):\n"
                                  L"        raise AssertionError"
                            : L"    return range(" + std::wstring(args) + L")";
            test::VmTestContext context;
            EXPECT_EQ(Value::True(),
                      context.run_file((prefix + body + suffix).c_str()));
        }
    }
}

TEST(Interpreter, range_big_integer_overflow_is_catchable_on_both_paths)
{
    for(int64_t integer: {value_smi_min - 1, value_smi_max + 1})
    {
        std::wstring huge = std::to_wstring(integer);
        for(const std::wstring &args:
            {huge, huge + L", 3", L"0, " + huge, L"0, 3, " + huge})
        {
            for(bool direct_loop: {false, true})
            {
                std::wstring body = direct_loop
                                        ? L"    for value in range(" + args +
                                              L"):\n"
                                              L"        raise AssertionError"
                                        : L"    return range(" + args + L")";
                std::wstring source =
                    L"def fail():\n" + body +
                    L"\ntry:\n    fail()\nexcept OverflowError as e:\n"
                    L"    assert str(e) == 'integer overflow'\n"
                    L"else: raise AssertionError\nTrue\n";
                test::VmTestContext context;
                EXPECT_EQ(Value::True(), context.run_file(source.c_str()));
            }
        }
    }
}

TEST(Interpreter, range_rebinding_preserves_direct_loop_fallback)
{
    test::VmTestContext context;
    EXPECT_EQ(Value::True(), context.run_file(LR"(
class I(int): pass
def range(stop): return [stop]
stop = I(3)
count = 0
for value in range(stop):
    assert value is stop
    count += 1
assert count == 1
True
)"));
}

TEST(Interpreter,
     native_and_compiler_errors_use_canonical_rebound_exception_classes)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ModuleObject *builtins = context.vm().global_builtins_module().extract();
    const wchar_t *names[] = {L"TypeError", L"AssertionError", L"NameError",
                              L"ValueError"};
    ClassObject *classes[std::size(names)];
    for(size_t idx = 0; idx < std::size(names); ++idx)
    {
        classes[idx] = context.thread()->class_for_builtin_name(names[idx]);
        ASSERT_TRUE(builtins->set_own_property(interned_string(names[idx]),
                                               Value::None()));
    }
    // Compile after rebinding: with-statement lowering embeds a canonical
    // TypeError, and runtime assertion/global/native errors must do the same.
    CodeObject *code = context.compile_file(LR"(
import builtins
assert TypeError is None
assert builtins.AssertionError is None
try:
    int.__new__(float, 4)
except OriginalTypeError:
    pass
else:
    assert False
try:
    with object():
        pass
except OriginalTypeError:
    pass
else:
    assert False
try:
    assert False
except OriginalAssertionError:
    pass
try:
    missing_global
except OriginalNameError:
    pass
else:
    assert False
try:
    int("invalid integer")
except OriginalValueError:
    pass
else:
    assert False
True
)");
    for(size_t idx = 0; idx < std::size(names); ++idx)
    {
        TValue<String> alias = context.vm().get_or_create_interned_string_value(
            std::wstring(L"Original") + names[idx]);
        ASSERT_TRUE(code->get_defining_module().extract()->set_own_property(
            alias, Value::from_oop(classes[idx])));
    }
    EXPECT_EQ(Value::True(), context.thread()->run_clovervm_code_object(code));
}
