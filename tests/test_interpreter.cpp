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
              cl_test_string_to_wstring(exception.extract()->message.value()));
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

static void expect_range_iterator(Value actual, int64_t expected_current,
                                  int64_t expected_stop, int64_t expected_step)
{
    RangeIterator *iterator = CL_ASSERT_CONVERT_TO(RangeIterator, actual);
    EXPECT_EQ(Value::from_smi(expected_current), iterator->current.raw_value());
    EXPECT_EQ(Value::from_smi(expected_stop), iterator->stop.raw_value());
    EXPECT_EQ(Value::from_smi(expected_step), iterator->step.raw_value());
}

static void expect_tuple_iterator(Value actual, Tuple *expected_tuple,
                                  int64_t expected_length,
                                  int64_t expected_index)
{
    TupleIterator *iterator = CL_ASSERT_CONVERT_TO(TupleIterator, actual);
    EXPECT_EQ(Value::from_oop(expected_tuple), iterator->tuple.raw_value());
    EXPECT_EQ(Value::from_smi(expected_length), iterator->length.raw_value());
    EXPECT_EQ(Value::from_smi(expected_index), iterator->index.raw_value());
}

static void expect_list_iterator(Value actual, List *expected_list,
                                 int64_t expected_index)
{
    ListIterator *iterator = CL_ASSERT_CONVERT_TO(ListIterator, actual);
    EXPECT_EQ(Value::from_oop(expected_list), iterator->list.raw_value());
    EXPECT_EQ(Value::from_smi(expected_index), iterator->index.raw_value());
}

static void expect_slice_indices_tuple(Value actual, int64_t expected_start,
                                       int64_t expected_stop,
                                       int64_t expected_step)
{
    ASSERT_TRUE(can_convert_to<Tuple>(actual));
    Tuple *tuple = assume_convert_to<Tuple>(actual);
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(expected_start), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(expected_stop), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(expected_step), tuple->item_unchecked(2));
}

static void expect_slice_indices(const wchar_t *source, int64_t expected_start,
                                 int64_t expected_stop, int64_t expected_step)
{
    test::FileRunner file_runner(source);
    expect_slice_indices_tuple(file_runner.return_value, expected_start,
                               expected_stop, expected_step);
}

static void expect_string_value(Value actual, const wchar_t *expected)
{
    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(expected,
                 string_as_wchar_t(TValue<String>::from_value_assumed(actual)));
}

static void expect_string_result(const wchar_t *source, const wchar_t *expected)
{
    test::FileRunner file_runner(source);
    expect_string_value(file_runner.return_value, expected);
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

static Value
load_builtin_from_module_for_test(test::VmTestContext &test_context,
                                  const wchar_t *name)
{
    TValue<String> name_value =
        test_context.vm().get_or_create_interned_string_value(name);
    return load_module_global(
        test_context.vm().global_builtins_module().extract(), name_value);
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

TEST(Interpreter, float_truthiness_asserts)
{
    test::VmTestContext test_context;

    Value truthy_result = test_context.run_file(L"assert 1.0\n");
    ASSERT_TRUE(can_convert_to<Float>(truthy_result));
    EXPECT_DOUBLE_EQ(1.0, truthy_result.get_ptr<Float>()->value);
    EXPECT_EQ(Value::True(), test_context.run_file(L"assert not 0.0\n"));
}

TEST(Interpreter, float_truthiness_if_statements)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"if 1.0:\n"
                                                        L"    result = 1\n"
                                                        L"else:\n"
                                                        L"    result = 2\n"
                                                        L"result\n"));
    EXPECT_EQ(Value::from_smi(2), test_context.run_file(L"if 0.0:\n"
                                                        L"    result = 1\n"
                                                        L"else:\n"
                                                        L"    result = 2\n"
                                                        L"result\n"));
}

TEST(Interpreter, float_truthiness_while_statement)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(1),
              test_context.run_file(L"guard = 1.0\n"
                                    L"count = 0\n"
                                    L"while guard:\n"
                                    L"    count = count + 1\n"
                                    L"    guard = 0.0\n"
                                    L"count\n"));
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

TEST(Interpreter, function_keyword_call_reorders_arguments)
{
    test::FileRunner file_runner(L"def f(a, b, c):\n"
                                 L"    return a * 100 + b * 10 + c\n"
                                 L"f(1, c=3, b=2)\n");
    EXPECT_EQ(Value::from_smi(123), file_runner.return_value);
}

TEST(Interpreter, function_keyword_call_cache_hit)
{
    test::FileRunner file_runner(L"def f(a, b):\n"
                                 L"    return a * 10 + b\n"
                                 L"f(a=1, b=2)\n"
                                 L"f(a=3, b=4)\n");
    EXPECT_EQ(Value::from_smi(34), file_runner.return_value);
}

TEST(Interpreter, function_keyword_call_uses_defaults)
{
    test::FileRunner file_runner(L"def f(a, b=1, c=2, d=4):\n"
                                 L"    return a * 1000 + b * 100 + c * 10 + d\n"
                                 L"f(7, 8, d=9)\n");
    EXPECT_EQ(Value::from_smi(7829), file_runner.return_value);
}

TEST(Interpreter, function_keyword_call_fills_required_before_default)
{
    test::FileRunner file_runner(L"def f(a, b=2):\n"
                                 L"    return a * 10 + b\n"
                                 L"f(a=3)\n");
    EXPECT_EQ(Value::from_smi(32), file_runner.return_value);
}

TEST(Interpreter, function_keyword_call_initializes_varargs)
{
    test::FileRunner file_runner(L"def f(a, b=2, *args):\n"
                                 L"    return a * 10 + b + len(args)\n"
                                 L"f(a=3)\n");
    EXPECT_EQ(Value::from_smi(32), file_runner.return_value);
}

TEST(Interpreter, function_keyword_call_initializes_keyword_only_defaults)
{
    test::FileRunner file_runner(L"def f(a, *, b=2, c=3):\n"
                                 L"    return a * 100 + b * 10 + c\n"
                                 L"f(4, c=5)\n");
    EXPECT_EQ(Value::from_smi(425), file_runner.return_value);
}

TEST(Interpreter, function_keyword_call_rejects_keyword_only_as_positional)
{
    expect_python_error(L"def f(a, *, b=2):\n"
                        L"    return a + b\n"
                        L"f(1, 2)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, function_keyword_call_supports_varargs_before_keyword_only)
{
    test::FileRunner file_runner(L"def f(*args, sep=9):\n"
                                 L"    return len(args) * 10 + sep\n"
                                 L"f(1, 2, sep=3)\n");
    EXPECT_EQ(Value::from_smi(23), file_runner.return_value);
}

TEST(Interpreter, function_keyword_call_supports_required_keyword_only)
{
    test::FileRunner file_runner(L"def f(a=1, *, b, c=3):\n"
                                 L"    return a * 100 + b * 10 + c\n"
                                 L"f(b=2)\n");
    EXPECT_EQ(Value::from_smi(123), file_runner.return_value);
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

TEST(Interpreter, function_keyword_call_handles_varargs_default_holes)
{
    test::FileRunner file_runner(L"def f(a=1, *args, b, c=3):\n"
                                 L"    return a * 1000 + len(args) * 100 + "
                                 L"b * 10 + c\n"
                                 L"f(5, 6, 7, b=8)\n");
    EXPECT_EQ(Value::from_smi(5283), file_runner.return_value);
}

TEST(Interpreter, class_constructor_accepts_keyword_calls)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __init__(self, a, b=2):\n"
                                 L"        self.value = a + b\n"
                                 L"C(a=3).value\n");
    EXPECT_EQ(Value::from_smi(5), file_runner.return_value);
}

TEST(Interpreter, class_constructor_preserves_keyword_only_defaults)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __init__(self, *, value=7):\n"
                                 L"        self.value = value\n"
                                 L"C().value\n");
    EXPECT_EQ(Value::from_smi(7), file_runner.return_value);
}

TEST(Interpreter, class_constructor_preserves_varargs_keyword_only_defaults)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __init__(self, *items, sep=9):\n"
                                 L"        self.value = len(items) * 10 + sep\n"
                                 L"C(1, 2).value\n");
    EXPECT_EQ(Value::from_smi(29), file_runner.return_value);
}

TEST(Interpreter, class_constructor_preserves_required_keyword_only)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __init__(self, a=1, *, b, c=3):\n"
                                 L"        self.value = a * 100 + b * 10 + c\n"
                                 L"C(b=2).value\n");
    EXPECT_EQ(Value::from_smi(123), file_runner.return_value);
}

TEST(Interpreter, class_constructor_drops_self_default_from_thunk_defaults)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __init__(self=0, *, value=7):\n"
                                 L"        self.value = value\n"
                                 L"C().value\n");
    EXPECT_EQ(Value::from_smi(7), file_runner.return_value);
}

TEST(Interpreter, class_constructor_calls_new_without_init)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __new__(cls):\n"
                                 L"        return 42\n"
                                 L"C()\n");
    EXPECT_EQ(Value::from_smi(42), file_runner.return_value);
}

TEST(Interpreter, class_constructor_new_only_passes_class)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __new__(cls):\n"
                                 L"        return cls is C\n"
                                 L"C()\n");
    EXPECT_EQ(Value::True(), file_runner.return_value);
}

TEST(Interpreter, class_constructor_new_only_preserves_defaults)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __new__(cls, value=7):\n"
                                 L"        return value\n"
                                 L"C()\n");
    EXPECT_EQ(Value::from_smi(7), file_runner.return_value);
}

TEST(Interpreter, class_constructor_new_only_accepts_keyword_calls)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __new__(cls, value=7):\n"
                                 L"        return value\n"
                                 L"C(value=11)\n");
    EXPECT_EQ(Value::from_smi(11), file_runner.return_value);
}

TEST(Interpreter, class_constructor_new_only_preserves_keyword_only_defaults)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __new__(cls, *, value=7):\n"
                                 L"        return value\n"
                                 L"C()\n");
    EXPECT_EQ(Value::from_smi(7), file_runner.return_value);
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

TEST(Interpreter, int_constructor_defaults_to_zero)
{
    test::FileRunner file_runner(L"int()\n");
    EXPECT_EQ(Value::from_smi(0), file_runner.return_value);
}

TEST(Interpreter, int_constructor_returns_int_argument)
{
    test::FileRunner file_runner(L"int(42)\n");
    EXPECT_EQ(Value::from_smi(42), file_runner.return_value);
}

TEST(Interpreter, int_constructor_converts_bool)
{
    test::FileRunner true_runner(L"int(True)\n");
    EXPECT_EQ(Value::from_smi(1), true_runner.return_value);

    test::FileRunner false_runner(L"int(False)\n");
    EXPECT_EQ(Value::from_smi(0), false_runner.return_value);
}

TEST(Interpreter, int_constructor_converts_string)
{
    EXPECT_EQ(Value::from_smi(123),
              test::FileRunner(L"int('123')\n").return_value);
    EXPECT_EQ(Value::from_smi(-42),
              test::FileRunner(L"int('  -42  ')\n").return_value);
    EXPECT_EQ(Value::from_smi(1000),
              test::FileRunner(L"int('1_000')\n").return_value);
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

TEST(Interpreter, int_constructor_reports_string_overflow)
{
    expect_python_error(L"int('288230376151711744')\n", L"OverflowError",
                        L"integer overflow");
    expect_python_error(L"int('-288230376151711745')\n", L"OverflowError",
                        L"integer overflow");
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

TEST(Interpreter, function_varargs_collect_empty_tuple)
{
    test::FileRunner file_runner(L"def f(*args):\n"
                                 "    return args\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_TRUE(TValue<Tuple>::from_value_assumed(actual).extract()->empty());
}

TEST(Interpreter, function_varargs_still_requires_positional_arguments)
{
    expect_python_error(L"def f(a, *args):\n"
                        L"    return a\n"
                        L"f()\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, class_definition_binds_class_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"Cls\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"Cls",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
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

TEST(Interpreter, class_call_allocates_instance)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"Cls()\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Instance,
              actual.get_ptr<Object>()->native_layout_id());
    ClassObject *actual_class =
        actual.get_ptr<Instance>()->get_shape()->get_class();
    ASSERT_NE(nullptr, actual_class);
    EXPECT_EQ(NativeLayoutId::ClassObject,
              actual_class->HeapObject::native_layout_id());
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

TEST(Interpreter, string_literal_value)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"\"abc\"\n");

    EXPECT_STREQ(L"abc",
                 string_as_wchar_t(TValue<String>::from_value_assumed(actual)));
}

TEST(Interpreter, str_constructor_no_args_returns_empty_string)
{
    test::FileRunner file_runner(L"str()\n");
    ASSERT_TRUE(can_convert_to<String>(file_runner.return_value));
    EXPECT_STREQ(L"", string_as_wchar_t(TValue<String>::from_value_assumed(
                          file_runner.return_value)));
}

TEST(Interpreter, str_constructor_returns_exact_string_argument)
{
    EXPECT_EQ(Value::True(), test::FileRunner(L"s = 'hello'\n"
                                              L"str(s) is s\n")
                                 .return_value);
}

TEST(Interpreter, str_constructor_converts_int)
{
    test::FileRunner file_runner(L"str(123)\n");
    ASSERT_TRUE(can_convert_to<String>(file_runner.return_value));
    EXPECT_STREQ(L"123", string_as_wchar_t(TValue<String>::from_value_assumed(
                             file_runner.return_value)));
}

TEST(Interpreter, str_constructor_converts_negative_int)
{
    test::FileRunner file_runner(L"str(-123)\n");
    ASSERT_TRUE(can_convert_to<String>(file_runner.return_value));
    EXPECT_STREQ(L"-123", string_as_wchar_t(TValue<String>::from_value_assumed(
                              file_runner.return_value)));
}

TEST(Interpreter, str_constructor_converts_bool_via_dunder_str)
{
    test::FileRunner file_runner(L"str(True)\n");
    ASSERT_TRUE(can_convert_to<String>(file_runner.return_value));
    EXPECT_STREQ(L"True", string_as_wchar_t(TValue<String>::from_value_assumed(
                              file_runner.return_value)));
}

TEST(Interpreter, str_constructor_calls_dunder_str)
{
    test::FileRunner file_runner(L"class C:\n"
                                 L"    def __str__(self):\n"
                                 L"        return 'custom'\n"
                                 L"str(C())\n");
    ASSERT_TRUE(can_convert_to<String>(file_runner.return_value));
    EXPECT_STREQ(L"custom",
                 string_as_wchar_t(TValue<String>::from_value_assumed(
                     file_runner.return_value)));
}

TEST(Interpreter, str_constructor_rejects_non_string_dunder_str_return)
{
    expect_python_error(L"class C:\n"
                        L"    def __str__(self):\n"
                        L"        return 123\n"
                        L"str(C())\n",
                        L"TypeError", L"__str__ returned non-str");
}

TEST(Interpreter, str_dunder_new_converts_value)
{
    test::FileRunner file_runner(L"str.__new__(str, 123)\n");
    ASSERT_TRUE(can_convert_to<String>(file_runner.return_value));
    EXPECT_STREQ(L"123", string_as_wchar_t(TValue<String>::from_value_assumed(
                             file_runner.return_value)));
}

TEST(Interpreter, float_literal_values)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"1\n"));

    auto expect_float_literal = [&](const wchar_t *source, double expected) {
        Value actual = test_context.run_file(source);
        ASSERT_TRUE(can_convert_to<Float>(actual));
        EXPECT_DOUBLE_EQ(expected, actual.get_ptr<Float>()->value);
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
    EXPECT_TRUE(std::isinf(huge_actual.get_ptr<Float>()->value));
    EXPECT_FALSE(std::signbit(huge_actual.get_ptr<Float>()->value));

    Value negative_huge_actual = test_context.run_file(L"-1e1000\n");
    ASSERT_TRUE(can_convert_to<Float>(negative_huge_actual));
    EXPECT_TRUE(std::isinf(negative_huge_actual.get_ptr<Float>()->value));
    EXPECT_TRUE(std::signbit(negative_huge_actual.get_ptr<Float>()->value));

    Value tiny_actual = test_context.run_file(L"1e-1000\n");
    ASSERT_TRUE(can_convert_to<Float>(tiny_actual));
    EXPECT_EQ(0.0, tiny_actual.get_ptr<Float>()->value);
    EXPECT_FALSE(std::signbit(tiny_actual.get_ptr<Float>()->value));

    Value negative_tiny_actual = test_context.run_file(L"-1e-1000\n");
    ASSERT_TRUE(can_convert_to<Float>(negative_tiny_actual));
    EXPECT_EQ(0.0, negative_tiny_actual.get_ptr<Float>()->value);
    EXPECT_TRUE(std::signbit(negative_tiny_actual.get_ptr<Float>()->value));

    std::wstring tiny_decimal_source = L"0." + std::wstring(400, L'0') + L"1\n";
    Value tiny_decimal_actual =
        test_context.run_file(tiny_decimal_source.c_str());
    ASSERT_TRUE(can_convert_to<Float>(tiny_decimal_actual));
    EXPECT_EQ(0.0, tiny_decimal_actual.get_ptr<Float>()->value);
    EXPECT_FALSE(std::signbit(tiny_decimal_actual.get_ptr<Float>()->value));
}

TEST(Interpreter, float_arithmetic_values)
{
    test::VmTestContext test_context;

    auto expect_float_result = [&](const wchar_t *source, double expected) {
        Value actual = test_context.run_file(source);
        ASSERT_TRUE(can_convert_to<Float>(actual));
        EXPECT_DOUBLE_EQ(expected, actual.get_ptr<Float>()->value);
    };

    expect_float_result(L"1.5 + 2.25\n", 3.75);
    expect_float_result(L"1.5 + 2\n", 3.5);
    expect_float_result(L"2 + 1.5\n", 3.5);
    expect_float_result(L"5.5 - 2.0\n", 3.5);
    expect_float_result(L"5.5 - 2\n", 3.5);
    expect_float_result(L"5 - 1.5\n", 3.5);
    expect_float_result(L"1.5 * 2.0\n", 3.0);
    expect_float_result(L"1.5 * 2\n", 3.0);
    expect_float_result(L"2 * 1.5\n", 3.0);
    expect_float_result(L"-1.5\n", -1.5);
    expect_float_result(L"+1.5\n", 1.5);
    EXPECT_EQ(Value::True(), test_context.run_file(L"assert not -0.0\n"));
    EXPECT_EQ(Value::from_smi(3), test_context.run_file(L"1 + 2\n"));
    EXPECT_EQ(Value::from_smi(3), test_context.run_file(L"5 - 2\n"));
    EXPECT_EQ(Value::from_smi(6), test_context.run_file(L"2 * 3\n"));
    EXPECT_EQ(Value::from_smi(2), test_context.run_file(L"True + True\n"));
    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"True - True\n"));
    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"True * False\n"));
    EXPECT_EQ(Value::from_smi(-1), test_context.run_file(L"-True\n"));
    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"+True\n"));
}

TEST(Interpreter, bitwise_integer_values)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"5 & 3\n"));
    EXPECT_EQ(Value::from_smi(5), test_context.run_file(L"4 | 1\n"));
    EXPECT_EQ(Value::from_smi(5), test_context.run_file(L"6 ^ 3\n"));
    EXPECT_EQ(Value::from_smi(-5), test_context.run_file(L"-8 | 3\n"));
    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"-8 & 3\n"));
    EXPECT_EQ(Value::from_smi(-5), test_context.run_file(L"-8 ^ 3\n"));
    EXPECT_EQ(Value::from_smi(-4), test_context.run_file(L"~3\n"));
    EXPECT_EQ(Value::from_smi(2), test_context.run_file(L"~-3\n"));
}

TEST(Interpreter, bitwise_dispatch_calls_custom_methods)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(42),
              test_context.run_file(L"class Right:\n"
                                    L"    def __rand__(self, other):\n"
                                    L"        return 42\n"
                                    L"1 & Right()\n"));
    EXPECT_EQ(Value::from_smi(43),
              test_context.run_file(L"class Right:\n"
                                    L"    def __ror__(self, other):\n"
                                    L"        return 43\n"
                                    L"1 | Right()\n"));
    EXPECT_EQ(Value::from_smi(44),
              test_context.run_file(L"class Right:\n"
                                    L"    def __rxor__(self, other):\n"
                                    L"        return 44\n"
                                    L"1 ^ Right()\n"));
    EXPECT_EQ(Value::from_smi(45),
              test_context.run_file(L"class Left:\n"
                                    L"    def __invert__(self):\n"
                                    L"        return 45\n"
                                    L"~Left()\n"));
}

TEST(Interpreter, binary_power_dispatch_calls_custom_methods)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(42),
              test_context.run_file(L"class Left:\n"
                                    L"    def __pow__(self, other):\n"
                                    L"        return 42\n"
                                    L"Left() ** 3\n"));
    EXPECT_EQ(Value::from_smi(43),
              test_context.run_file(L"class Right:\n"
                                    L"    def __rpow__(self, other):\n"
                                    L"        return 43\n"
                                    L"2 ** Right()\n"));
    EXPECT_EQ(Value::from_smi(44),
              test_context.run_file(L"class Left:\n"
                                    L"    def __pow__(self, other, mod=None):\n"
                                    L"        if mod is None:\n"
                                    L"            return 44\n"
                                    L"        return 45\n"
                                    L"Left() ** 3\n"));
}

TEST(Interpreter, builtin_pow_uses_binary_dispatch_when_modulo_is_none)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(42),
              test_context.run_file(L"class Left:\n"
                                    L"    def __pow__(self, other):\n"
                                    L"        return 42\n"
                                    L"pow(Left(), 3)\n"));
    EXPECT_EQ(Value::from_smi(43),
              test_context.run_file(L"class Left:\n"
                                    L"    def __pow__(self, other):\n"
                                    L"        return 43\n"
                                    L"pow(Left(), 3, None)\n"));
}

TEST(Interpreter, builtin_pow_uses_ternary_dispatch_for_modulo)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(305),
              test_context.run_file(L"class Left:\n"
                                    L"    def __pow__(self, other, modulo):\n"
                                    L"        return other * 100 + modulo\n"
                                    L"pow(Left(), 3, 5)\n"));
    EXPECT_EQ(Value::from_smi(45),
              test_context.run_file(L"class Right:\n"
                                    L"    def __rpow__(self, other, modulo):\n"
                                    L"        return 45\n"
                                    L"pow(2, Right(), 5)\n"));
}

TEST(Interpreter, builtin_pow_with_modulo_rejects_binary_only_pow)
{
    expect_python_error(L"class Left:\n"
                        L"    def __pow__(self, other):\n"
                        L"        return 42\n"
                        L"pow(Left(), 3, 5)\n",
                        L"TypeError", L"wrong number of arguments");
}

TEST(Interpreter, true_division_values)
{
    test::VmTestContext test_context;

    auto expect_float_result = [&](const wchar_t *source, double expected) {
        Value actual = test_context.run_file(source);
        ASSERT_TRUE(can_convert_to<Float>(actual));
        EXPECT_DOUBLE_EQ(expected, actual.get_ptr<Float>()->value);
    };

    expect_float_result(L"1 / 2\n", 0.5);
    expect_float_result(L"1.0 / 2\n", 0.5);
    expect_float_result(L"1 / 2.0\n", 0.5);
    expect_float_result(L"1.0 / 2.0\n", 0.5);
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

TEST(Interpreter, floor_division_values)
{
    test::VmTestContext test_context;

    auto expect_float_result = [&](const wchar_t *source, double expected) {
        Value actual = test_context.run_file(source);
        ASSERT_TRUE(can_convert_to<Float>(actual));
        EXPECT_DOUBLE_EQ(expected, actual.get_ptr<Float>()->value);
    };

    EXPECT_EQ(Value::from_smi(2), test_context.run_file(L"5 // 2\n"));
    EXPECT_EQ(Value::from_smi(-3), test_context.run_file(L"-5 // 2\n"));
    EXPECT_EQ(Value::from_smi(-3), test_context.run_file(L"5 // -2\n"));
    EXPECT_EQ(Value::from_smi(2), test_context.run_file(L"-5 // -2\n"));
    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"True // 1\n"));
    expect_float_result(L"5.0 // 2\n", 2.0);
    expect_float_result(L"5 // 2.0\n", 2.0);
    expect_float_result(L"-5.0 // 2\n", -3.0);
}

TEST(Interpreter, floor_division_reports_errors)
{
    expect_python_error(L"1 // 0\n", L"ZeroDivisionError", L"division by zero");
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

TEST(Interpreter, modulo_values)
{
    test::VmTestContext test_context;

    auto expect_float_result = [&](const wchar_t *source, double expected) {
        Value actual = test_context.run_file(source);
        ASSERT_TRUE(can_convert_to<Float>(actual));
        EXPECT_DOUBLE_EQ(expected, actual.get_ptr<Float>()->value);
    };

    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"5 % 2\n"));
    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"-5 % 2\n"));
    EXPECT_EQ(Value::from_smi(-1), test_context.run_file(L"5 % -2\n"));
    EXPECT_EQ(Value::from_smi(-1), test_context.run_file(L"-5 % -2\n"));
    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"False % 1\n"));
    expect_float_result(L"5.0 % 2\n", 1.0);
    expect_float_result(L"5 % 2.0\n", 1.0);
    expect_float_result(L"-5.0 % 2\n", 1.0);
    expect_float_result(L"5.0 % -2\n", -1.0);
}

TEST(Interpreter, modulo_reports_errors)
{
    expect_python_error(L"1 % 0\n", L"ZeroDivisionError", L"division by zero");
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

TEST(Interpreter, operator_eq_dispatch_returns_non_bool_result_unchanged)
{
    expect_string_result(L"class EqResult:\n"
                         L"    def __eq__(self, other):\n"
                         L"        return 'sentinel'\n"
                         L"EqResult() == EqResult()\n",
                         L"sentinel");
}

TEST(Interpreter, rich_comparison_operator_dispatch_returns_non_bool_results)
{
    expect_string_result(L"class CompareResult:\n"
                         L"    def __ne__(self, other):\n"
                         L"        return 'ne'\n"
                         L"CompareResult() != CompareResult()\n",
                         L"ne");
    expect_string_result(L"class CompareResult:\n"
                         L"    def __lt__(self, other):\n"
                         L"        return 'lt'\n"
                         L"CompareResult() < CompareResult()\n",
                         L"lt");
    expect_string_result(L"class CompareResult:\n"
                         L"    def __le__(self, other):\n"
                         L"        return 'le'\n"
                         L"CompareResult() <= CompareResult()\n",
                         L"le");
    expect_string_result(L"class CompareResult:\n"
                         L"    def __gt__(self, other):\n"
                         L"        return 'gt'\n"
                         L"CompareResult() > CompareResult()\n",
                         L"gt");
    expect_string_result(L"class CompareResult:\n"
                         L"    def __ge__(self, other):\n"
                         L"        return 'ge'\n"
                         L"CompareResult() >= CompareResult()\n",
                         L"ge");
}

TEST(Interpreter, operator_eq_dispatch_identity_fallback_after_notimplemented)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(10),
              test_context.run_file(L"class EqResult:\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        return NotImplemented\n"
                                    L"same = EqResult()\n"
                                    L"different = EqResult()\n"
                                    L"result = 0\n"
                                    L"if same == same:\n"
                                    L"    result += 10\n"
                                    L"if same == different:\n"
                                    L"    result += 1\n"
                                    L"result\n"));
}

TEST(Interpreter, operator_ne_dispatch_inverts_object_eq_fallback)
{
    test::VmTestContext test_context;

    EXPECT_EQ(
        Value::from_smi(11),
        test_context.run_file(L"class AlwaysEqual:\n"
                              L"    def __eq__(self, other):\n"
                              L"        return True\n"
                              L"class NeverEqual:\n"
                              L"    def __eq__(self, other):\n"
                              L"        return False\n"
                              L"result = 0\n"
                              L"if not (AlwaysEqual() != AlwaysEqual()):\n"
                              L"    result += 10\n"
                              L"if NeverEqual() != NeverEqual():\n"
                              L"    result += 1\n"
                              L"result\n"));
}

TEST(Interpreter, operator_eq_dispatch_same_exact_type_double_dispatch)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(2),
              test_context.run_file(L"class EqResult:\n"
                                    L"    count = 0\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        EqResult.count += 1\n"
                                    L"        return NotImplemented\n"
                                    L"EqResult() == EqResult()\n"
                                    L"EqResult.count\n"));
}

TEST(Interpreter, operator_eq_dispatch_right_subclass_reflected_priority)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(7),
              test_context.run_file(L"class Base:\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        return 3\n"
                                    L"class Derived(Base):\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        return 7\n"
                                    L"Base() == Derived()\n"));
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

TEST(Interpreter, operator_add_dispatch_calls_python_dunder_from_add_smi)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(11),
              test_context.run_file(L"class Addable:\n"
                                    L"    def __add__(self, other):\n"
                                    L"        return 11\n"
                                    L"Addable() + 1\n"));
}

TEST(Interpreter, operator_add_dispatch_continues_after_notimplemented)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(7),
              test_context.run_file(L"class Left:\n"
                                    L"    def __add__(self, other):\n"
                                    L"        return NotImplemented\n"
                                    L"class Right:\n"
                                    L"    def __radd__(self, other):\n"
                                    L"        return 7\n"
                                    L"Left() + Right()\n"));
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    EXPECT_TRUE(cache.reflected_untrusted_call);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_add_dispatch_inherited_radd_does_not_preempt_add)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(3),
              test_context.run_file(L"class Base:\n"
                                    L"    def __add__(self, other):\n"
                                    L"        return 3\n"
                                    L"    def __radd__(self, other):\n"
                                    L"        return 7\n"
                                    L"class Derived(Base):\n"
                                    L"    pass\n"
                                    L"Base() + Derived()\n"));
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
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
    EXPECT_DOUBLE_EQ(3.5, actual.get_ptr<Float>()->value);

    Value function_value =
        load_global_from_module_for_test(code_obj, function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
    EXPECT_FALSE(cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
    EXPECT_FALSE(cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, operator_eq_dispatch_reflected_float_trusted_cache_hit)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(11),
              test_context.run_file(L"def eq(left, right):\n"
                                    L"    return left == right\n"
                                    L"result = 0\n"
                                    L"if eq(1, 1.0):\n"
                                    L"    result += 10\n"
                                    L"if eq(1, 1.0):\n"
                                    L"    result += 1\n"
                                    L"result\n"));
}

TEST(Interpreter, operator_lt_dispatch_reflected_float_trusted_cache_hit)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(0),
              test_context.run_file(L"def lt(left, right):\n"
                                    L"    return left < right\n"
                                    L"result = 0\n"
                                    L"if lt(2, 1.0):\n"
                                    L"    result += 10\n"
                                    L"if lt(2, 1.0):\n"
                                    L"    result += 1\n"
                                    L"result\n"));
}

TEST(Interpreter, operator_eq_dispatch_reloads_after_notimplemented)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(101),
              test_context.run_file(L"class EqResult:\n"
                                    L"    count = 0\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        EqResult.count += 1\n"
                                    L"        def replacement(self, other):\n"
                                    L"            EqResult.count += 100\n"
                                    L"            return 42\n"
                                    L"        EqResult.__eq__ = replacement\n"
                                    L"        return NotImplemented\n"
                                    L"EqResult() == EqResult()\n"
                                    L"EqResult.count\n"));
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[1]);
}

TEST(Interpreter, shortcutting_boolean_operators_return_operand_values)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"0 and missing\n"));
    EXPECT_EQ(Value::from_smi(7), test_context.run_file(L"1 and 7\n"));
    EXPECT_EQ(Value::from_smi(5), test_context.run_file(L"5 or missing\n"));
    EXPECT_EQ(Value::from_smi(8), test_context.run_file(L"0 or 8\n"));
}

TEST(Interpreter, shortcutting_boolean_operators_skip_unneeded_operand)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(4),
              test_context.run_file(L"def fail():\n"
                                    L"    raise ValueError\n"
                                    L"if True or fail():\n"
                                    L"    x = 4\n"
                                    L"else:\n"
                                    L"    x = 1\n"
                                    L"x\n"));
    EXPECT_EQ(Value::from_smi(4),
              test_context.run_file(L"def fail():\n"
                                    L"    raise ValueError\n"
                                    L"if False and fail():\n"
                                    L"    x = 1\n"
                                    L"else:\n"
                                    L"    x = 4\n"
                                    L"x\n"));
}

TEST(Interpreter, shortcutting_boolean_operators_compile_math_shaped_cases)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::True(), test_context.run_file(
                                 L"def isinf(x):\n"
                                 L"    return False\n"
                                 L"def isnan(x):\n"
                                 L"    return False\n"
                                 L"def finite(x):\n"
                                 L"    return not isinf(x) and not isnan(x)\n"
                                 L"finite(1)\n"));
    EXPECT_EQ(Value::from_smi(3),
              test_context.run_file(L"result = 0\n"
                                    L"value = 1\n"
                                    L"if result == 0 or value == 0:\n"
                                    L"    result = 3\n"
                                    L"result\n"));
}

TEST(Interpreter, sqrt_is_not_a_builtin)
{
    expect_python_error(L"sqrt(4)\n", L"NameError",
                        L"name 'sqrt' is not defined");
}

TEST(Interpreter, float_comparison_values)
{
    test::VmTestContext test_context;

    auto expect_bool_result = [&](const wchar_t *source, Value expected) {
        EXPECT_EQ(expected, test_context.run_file(source));
    };

    expect_bool_result(L"1.0 < 2.0\n", Value::True());
    expect_bool_result(L"2.0 < 1.0\n", Value::False());
    expect_bool_result(L"1.0 <= 1.0\n", Value::True());
    expect_bool_result(L"2.0 <= 1.0\n", Value::False());
    expect_bool_result(L"2.0 > 1.0\n", Value::True());
    expect_bool_result(L"1.0 > 2.0\n", Value::False());
    expect_bool_result(L"2.0 >= 2.0\n", Value::True());
    expect_bool_result(L"1.0 >= 2.0\n", Value::False());
    expect_bool_result(L"1.0 == 1.0\n", Value::True());
    expect_bool_result(L"1.0 == 2.0\n", Value::False());
    expect_bool_result(L"1.0 != 2.0\n", Value::True());
    expect_bool_result(L"1.0 != 1.0\n", Value::False());

    expect_bool_result(L"1 < 2.0\n", Value::True());
    expect_bool_result(L"2 < 1.0\n", Value::False());
    expect_bool_result(L"1 <= 1.0\n", Value::True());
    expect_bool_result(L"2 <= 1.0\n", Value::False());
    expect_bool_result(L"2 > 1.0\n", Value::True());
    expect_bool_result(L"1 > 2.0\n", Value::False());
    expect_bool_result(L"2 >= 2.0\n", Value::True());
    expect_bool_result(L"1 >= 2.0\n", Value::False());
    expect_bool_result(L"1 == 1.0\n", Value::True());
    expect_bool_result(L"1 == 2.0\n", Value::False());
    expect_bool_result(L"1 != 2.0\n", Value::True());
    expect_bool_result(L"1 != 1.0\n", Value::False());

    expect_bool_result(L"1.0 < 2\n", Value::True());
    expect_bool_result(L"2.0 < 1\n", Value::False());
    expect_bool_result(L"1.0 <= 1\n", Value::True());
    expect_bool_result(L"2.0 <= 1\n", Value::False());
    expect_bool_result(L"2.0 > 1\n", Value::True());
    expect_bool_result(L"1.0 > 2\n", Value::False());
    expect_bool_result(L"2.0 >= 2\n", Value::True());
    expect_bool_result(L"1.0 >= 2\n", Value::False());
    expect_bool_result(L"1.0 == 1\n", Value::True());
    expect_bool_result(L"1.0 == 2\n", Value::False());
    expect_bool_result(L"1.0 != 2\n", Value::True());
    expect_bool_result(L"1.0 != 1\n", Value::False());
    expect_bool_result(L"True == 1.0\n", Value::True());
    expect_bool_result(L"False == 0.0\n", Value::True());
    expect_bool_result(L"True != 1.0\n", Value::False());
    expect_bool_result(L"False != 0.0\n", Value::False());
    expect_bool_result(L"True < 2.0\n", Value::True());
    expect_bool_result(L"False >= 0.0\n", Value::True());

    expect_bool_result(L"0.0 == -0.0\n", Value::True());
    expect_bool_result(L"0.0 != -0.0\n", Value::False());
    expect_bool_result(L"0.0 <= -0.0\n", Value::True());
    expect_bool_result(L"0.0 >= -0.0\n", Value::True());

    expect_bool_result(L"inf = 1e300 * 1e300\n"
                       L"nan = inf / inf\n"
                       L"nan < nan\n",
                       Value::False());
    expect_bool_result(L"inf = 1e300 * 1e300\n"
                       L"nan = inf / inf\n"
                       L"nan <= nan\n",
                       Value::False());
    expect_bool_result(L"inf = 1e300 * 1e300\n"
                       L"nan = inf / inf\n"
                       L"nan > nan\n",
                       Value::False());
    expect_bool_result(L"inf = 1e300 * 1e300\n"
                       L"nan = inf / inf\n"
                       L"nan >= nan\n",
                       Value::False());
    expect_bool_result(L"inf = 1e300 * 1e300\n"
                       L"nan = inf / inf\n"
                       L"nan == nan\n",
                       Value::False());
    expect_bool_result(L"inf = 1e300 * 1e300\n"
                       L"nan = inf / inf\n"
                       L"nan != nan\n",
                       Value::True());
}

TEST(Interpreter, string_comparison_values)
{
    test::VmTestContext test_context;

    auto expect_bool_result = [&](const wchar_t *source, Value expected) {
        EXPECT_EQ(expected, test_context.run_file(source));
    };

    expect_bool_result(L"\"a\" == \"a\"\n", Value::True());
    expect_bool_result(L"\"a\" == \"b\"\n", Value::False());
    expect_bool_result(L"\"a\" != \"a\"\n", Value::False());
    expect_bool_result(L"\"a\" != \"b\"\n", Value::True());
    expect_bool_result(L"\"a\" < \"b\"\n", Value::True());
    expect_bool_result(L"\"b\" < \"a\"\n", Value::False());
    expect_bool_result(L"\"a\" <= \"a\"\n", Value::True());
    expect_bool_result(L"\"b\" <= \"a\"\n", Value::False());
    expect_bool_result(L"\"b\" > \"a\"\n", Value::True());
    expect_bool_result(L"\"a\" > \"b\"\n", Value::False());
    expect_bool_result(L"\"a\" >= \"a\"\n", Value::True());
    expect_bool_result(L"\"a\" >= \"b\"\n", Value::False());
    expect_bool_result(L"\"a\" < \"aa\"\n", Value::True());

    expect_python_error(L"\"a\" < 1\n", L"TypeError",
                        L"unsupported operand type(s) for comparison");
}

TEST(Interpreter, string_add_expression_concatenates_strings)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"\"ab\" + \"cd\"\n");

    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(L"abcd",
                 string_as_wchar_t(TValue<String>::from_value_assumed(actual)));
}

TEST(Interpreter, string_dunder_comparisons_call_intrinsic_functions)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::True(), test_context.run_file(L"\"a\".__eq__(\"a\")\n"));
    EXPECT_EQ(Value::False(), test_context.run_file(L"\"a\".__eq__(\"b\")\n"));
    EXPECT_EQ(Value::False(), test_context.run_file(L"\"a\".__ne__(\"a\")\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"\"a\".__ne__(\"b\")\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"\"a\".__lt__(\"b\")\n"));
    EXPECT_EQ(Value::False(), test_context.run_file(L"\"b\".__lt__(\"a\")\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"\"a\".__le__(\"a\")\n"));
    EXPECT_EQ(Value::False(), test_context.run_file(L"\"b\".__le__(\"a\")\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"\"b\".__gt__(\"a\")\n"));
    EXPECT_EQ(Value::False(), test_context.run_file(L"\"a\".__gt__(\"b\")\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"\"a\".__ge__(\"a\")\n"));
    EXPECT_EQ(Value::False(), test_context.run_file(L"\"a\".__ge__(\"b\")\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"\"a\".__lt__(\"aa\")\n"));
    EXPECT_EQ(Value::NotImplemented(),
              test_context.run_file(L"\"a\".__lt__(1)\n"));
}

TEST(Interpreter, string_dunder_add_wrong_type_returns_notimplemented)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::NotImplemented(),
              test_context.run_file(L"\"ab\".__add__(3)\n"));
}

TEST(Interpreter,
     string_dunder_add_wrong_type_returns_notimplemented_from_nested_frame)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::NotImplemented(),
              test_context.run_file(L"def fail():\n"
                                    L"    return \"ab\".__add__(3)\n"
                                    L"fail()\n"));
}

TEST(Interpreter, list_literal_returns_list_object)
{
    test::FileRunner file_runner(L"[1, 2, 4]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), list->item_unchecked(2));
}

TEST(Interpreter, empty_list_literal_returns_empty_list)
{
    test::FileRunner file_runner(L"[]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    EXPECT_TRUE(list->empty());
    EXPECT_EQ(0u, list->size());
}

TEST(Interpreter, list_literal_evaluates_elements_left_to_right)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(
        L"[next_counter(), next_counter(), next_counter()]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_intrinsic_function(&test_context.vm(), native_next_counter)
            .value()
            .raw_value();
    ASSERT_TRUE(store_module_global(code_obj->get_defining_module().extract(),
                                    name, next_counter));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_smi(0), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(2));
}

TEST(Interpreter, tuple_literal_returns_tuple_object)
{
    test::FileRunner file_runner(L"1, 2, 4\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), tuple->item_unchecked(2));
}

TEST(Interpreter, parenthesized_tuple_literal_returns_tuple_object)
{
    test::FileRunner file_runner(L"(1, 2, 4)\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), tuple->item_unchecked(2));
}

TEST(Interpreter, empty_tuple_literal_returns_empty_tuple_object)
{
    test::FileRunner file_runner(L"()\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    EXPECT_EQ(0u, tuple->size());
}

TEST(Interpreter, tuple_literal_evaluates_elements_left_to_right)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(
        L"next_counter(), next_counter(), next_counter()\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_intrinsic_function(&test_context.vm(), native_next_counter)
            .value()
            .raw_value();
    ASSERT_TRUE(store_module_global(code_obj->get_defining_module().extract(),
                                    name, next_counter));

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(0), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(2));
}

TEST(Interpreter, subscript_load_reads_tuple_item)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"Cls.__mro__[0]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"Cls",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, subscript_load_reads_tuple_item_with_negative_index)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"Cls.__mro__[-1]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"object",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, subscript_load_calls_user_defined_dunder_getitem)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        return key + 3\n"
                                 L"Bag()[4]\n");

    EXPECT_EQ(Value::from_smi(7), file_runner.return_value);
}

TEST(Interpreter, subscript_load_calls_dunder_getitem_every_time)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.count = 0\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        self.count += 1\n"
                                 L"        return key + self.count\n"
                                 L"bag = Bag()\n"
                                 L"first = bag[10]\n"
                                 L"second = bag[10]\n"
                                 L"first * 100 + second\n");

    EXPECT_EQ(Value::from_smi(1112), file_runner.return_value);
}

TEST(Interpreter, subscript_load_returns_notimplemented_from_dunder_getitem)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        return NotImplemented\n"
                                 L"Bag()[0]\n");

    EXPECT_EQ(Value::NotImplemented(), file_runner.return_value);
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

TEST(Interpreter, slice_repr_shows_all_three_fields)
{
    test::FileRunner file_runner(L"repr(slice(1, 2, None))\n");

    ASSERT_TRUE(can_convert_to<String>(file_runner.return_value));
    EXPECT_STREQ(L"slice(1, 2, None)",
                 string_as_wchar_t(TValue<String>::from_value_assumed(
                     file_runner.return_value)));
}

TEST(Interpreter, slice_indices_normalizes_smi_fields)
{
    expect_slice_indices(L"slice(None, None).indices(5)\n", 0, 5, 1);
    expect_slice_indices(L"slice(None, None, -1).indices(5)\n", 4, -1, -1);
    expect_slice_indices(L"slice(0, -1).indices(5)\n", 0, 4, 1);
    expect_slice_indices(L"slice(-10, 10).indices(5)\n", 0, 5, 1);
    expect_slice_indices(L"slice(10, -10, -2).indices(5)\n", 4, -1, -2);
    expect_slice_indices(L"slice(1, 8, 2).indices(10)\n", 1, 8, 2);
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

TEST(Interpreter, subscript_load_observes_replaced_dunder_getitem)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        return 1\n"
                                 L"bag = Bag()\n"
                                 L"first = bag[0]\n"
                                 L"def replacement(self, key):\n"
                                 L"    return 2\n"
                                 L"Bag.__getitem__ = replacement\n"
                                 L"first * 10 + bag[0]\n");

    EXPECT_EQ(Value::from_smi(12), file_runner.return_value);
}

TEST(Interpreter, subscript_load_observes_replaced_inherited_dunder_getitem)
{
    test::FileRunner file_runner(L"class Base:\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        return 1\n"
                                 L"class Bag(Base):\n"
                                 L"    pass\n"
                                 L"def get(obj, key):\n"
                                 L"    return obj[key]\n"
                                 L"bag = Bag()\n"
                                 L"first = get(bag, 0)\n"
                                 L"def replacement(self, key):\n"
                                 L"    return 2\n"
                                 L"Base.__getitem__ = replacement\n"
                                 L"first * 10 + get(bag, 0)\n");

    EXPECT_EQ(Value::from_smi(12), file_runner.return_value);
}

TEST(Interpreter, subscript_load_does_not_cache_negative_lookup)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    pass\n"
                                 L"def get(obj, key):\n"
                                 L"    return obj[key]\n"
                                 L"bag = Bag()\n"
                                 L"saw_type_error = False\n"
                                 L"try:\n"
                                 L"    get(bag, 0)\n"
                                 L"except TypeError:\n"
                                 L"    saw_type_error = True\n"
                                 L"def replacement(self, key):\n"
                                 L"    return key + 9\n"
                                 L"Bag.__getitem__ = replacement\n"
                                 L"if saw_type_error:\n"
                                 L"    get(bag, 3)\n"
                                 L"else:\n"
                                 L"    0\n");

    EXPECT_EQ(Value::from_smi(12), file_runner.return_value);
}

TEST(Interpreter, subscript_load_does_not_cache_raised_exception)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.count = 0\n"
                                 L"    def __getitem__(self, key):\n"
                                 L"        self.count += 1\n"
                                 L"        if key == 1:\n"
                                 L"            raise ValueError\n"
                                 L"        return key + self.count * 10\n"
                                 L"def get(obj, key):\n"
                                 L"    return obj[key]\n"
                                 L"bag = Bag()\n"
                                 L"first = get(bag, 0)\n"
                                 L"caught = 0\n"
                                 L"try:\n"
                                 L"    get(bag, 1)\n"
                                 L"except ValueError:\n"
                                 L"    caught = 1\n"
                                 L"third = get(bag, 2)\n"
                                 L"first * 100 + caught * 10 + third\n");

    EXPECT_EQ(Value::from_smi(1042), file_runner.return_value);
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
    ASSERT_NE(nullptr, cache.function);
    EXPECT_EQ(2u, cache.n_args);
    EXPECT_TRUE(cache.has_self);
    EXPECT_EQ(FunctionCallAdaptation::Defaults, cache.adaptation);
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
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
    ASSERT_EQ(2u, function_code->operator_caches.size());
    const OperatorInlineCache &general_cache =
        function_code->operator_caches[0];
    const OperatorInlineCache &none_cache = function_code->operator_caches[1];
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
            function_code->operator_caches)
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

TEST(Interpreter, subscript_store_calls_user_defined_dunder_setitem)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.total = 0\n"
                                 L"    def __setitem__(self, key, value):\n"
                                 L"        self.total = key * 10 + value\n"
                                 L"bag = Bag()\n"
                                 L"bag[4] = 7\n"
                                 L"bag.total\n");

    EXPECT_EQ(Value::from_smi(47), file_runner.return_value);
}

TEST(Interpreter, subscript_store_observes_replaced_dunder_setitem)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.total = 0\n"
                                 L"    def __setitem__(self, key, value):\n"
                                 L"        self.total = 1\n"
                                 L"def set_item(obj, key, value):\n"
                                 L"    obj[key] = value\n"
                                 L"bag = Bag()\n"
                                 L"set_item(bag, 0, 0)\n"
                                 L"first = bag.total\n"
                                 L"def replacement(self, key, value):\n"
                                 L"    self.total = 2\n"
                                 L"Bag.__setitem__ = replacement\n"
                                 L"set_item(bag, 0, 0)\n"
                                 L"first * 10 + bag.total\n");

    EXPECT_EQ(Value::from_smi(12), file_runner.return_value);
}

TEST(Interpreter, subscript_assignment_evaluates_rhs_before_target)
{
    test::FileRunner file_runner(L"xs = [0, 0]\n"
                                 L"i = 0\n"
                                 L"def rhs():\n"
                                 L"    global i\n"
                                 L"    i = 1\n"
                                 L"    return 7\n"
                                 L"xs[i] = rhs()\n"
                                 L"xs[0] * 10 + xs[1]\n");

    EXPECT_EQ(Value::from_smi(7), file_runner.return_value);
}

TEST(Interpreter, annotated_subscript_assignment_evaluates_rhs_before_target)
{
    test::FileRunner file_runner(L"xs = [0, 0]\n"
                                 L"i = 0\n"
                                 L"def rhs():\n"
                                 L"    global i\n"
                                 L"    i = 1\n"
                                 L"    return 7\n"
                                 L"xs[i]: int = rhs()\n"
                                 L"xs[0] * 10 + xs[1]\n");

    EXPECT_EQ(Value::from_smi(7), file_runner.return_value);
}

TEST(Interpreter,
     slice_subscript_assignment_evaluates_rhs_then_target_then_slice)
{
    test::FileRunner file_runner(
        L"order = 0\n"
        L"def mark(expected):\n"
        L"    global order\n"
        L"    if order == expected:\n"
        L"        order += 1\n"
        L"        return 1\n"
        L"    return 100\n"
        L"def rhs():\n"
        L"    return mark(0)\n"
        L"def make_obj():\n"
        L"    mark(1)\n"
        L"    return Bag()\n"
        L"def start():\n"
        L"    return mark(2)\n"
        L"def stop():\n"
        L"    return mark(3)\n"
        L"class Bag:\n"
        L"    def __setitem__(self, key, value):\n"
        L"        global order\n"
        L"        if value == 1 and key.start == 1 and key.stop == 1 and "
        L"key.step is None and order == 4:\n"
        L"            order = 9\n"
        L"make_obj()[start():stop()] = rhs()\n"
        L"order\n");

    EXPECT_EQ(Value::from_smi(9), file_runner.return_value);
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
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
    ASSERT_EQ(1u, function_code->operator_caches.size());
    const OperatorInlineCache &cache = function_code->operator_caches[0];
    ASSERT_NE(nullptr, cache.operand_lookup_validity_cells[0]);
    EXPECT_EQ(ShapeKey::from_value(Value::from_smi(0)),
              cache.operand_shape_keys[1]);
    ASSERT_NE(nullptr, cache.function);
    EXPECT_EQ(3u, cache.n_args);
}

TEST(Interpreter, subscript_delete_calls_user_defined_dunder_delitem)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.total = 0\n"
                                 L"    def __delitem__(self, key):\n"
                                 L"        self.total = key + 5\n"
                                 L"bag = Bag()\n"
                                 L"del bag[4]\n"
                                 L"bag.total\n");

    EXPECT_EQ(Value::from_smi(9), file_runner.return_value);
}

TEST(Interpreter, subscript_delete_observes_replaced_dunder_delitem)
{
    test::FileRunner file_runner(L"class Bag:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.total = 0\n"
                                 L"    def __delitem__(self, key):\n"
                                 L"        self.total = 1\n"
                                 L"def del_item(obj, key):\n"
                                 L"    del obj[key]\n"
                                 L"bag = Bag()\n"
                                 L"del_item(bag, 0)\n"
                                 L"first = bag.total\n"
                                 L"def replacement(self, key):\n"
                                 L"    self.total = 2\n"
                                 L"Bag.__delitem__ = replacement\n"
                                 L"del_item(bag, 0)\n"
                                 L"first * 10 + bag.total\n");

    EXPECT_EQ(Value::from_smi(12), file_runner.return_value);
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
    EXPECT_EQ(Value::from_smi(7), dict->get_item(alpha));
    EXPECT_EQ(Value::from_smi(9), dict->get_item(beta));
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
                        L"xs[False]\n",
                        L"TypeError",
                        L"list indices must be integers or slices");
}

TEST(Interpreter, subscript_load_non_integer_list_index_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = [1, 2, 3]\n"
                        L"    return xs[False]\n"
                        L"fail()\n",
                        L"TypeError",
                        L"list indices must be integers or slices");
}

TEST(Interpreter, subscript_load_rejects_non_integer_tuple_index)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls.__mro__[False]\n",
                        L"TypeError",
                        L"tuple indices must be integers or slices");
}

TEST(Interpreter, subscript_load_slices_list)
{
    expect_string_result(L"repr([1, 2, 3, 4][1:3])\n", L"[2, 3]");
    expect_string_result(L"repr([1, 2, 3, 4][0:-1])\n", L"[1, 2, 3]");
    expect_string_result(L"repr([1, 2, 3, 4][::2])\n", L"[1, 3]");
    expect_string_result(L"repr([1, 2, 3, 4][::-1])\n", L"[4, 3, 2, 1]");
    expect_string_result(L"repr([1, 2, 3, 4][3:0:-1])\n", L"[4, 3, 2]");
}

TEST(Interpreter, subscript_load_slices_tuple)
{
    expect_string_result(L"repr((1, 2, 3, 4)[1:3])\n", L"(2, 3)");
    expect_string_result(L"repr((1, 2, 3, 4)[0:-1])\n", L"(1, 2, 3)");
    expect_string_result(L"repr((1, 2, 3, 4)[::2])\n", L"(1, 3)");
    expect_string_result(L"repr((1, 2, 3, 4)[::-1])\n", L"(4, 3, 2, 1)");
    expect_string_result(L"repr((1, 2, 3, 4)[3:0:-1])\n", L"(4, 3, 2)");
}

TEST(Interpreter, subscript_load_slices_string)
{
    expect_string_result(L"'abcd'[1:3]\n", L"bc");
    expect_string_result(L"'abcd'[0:-1]\n", L"abc");
    expect_string_result(L"'abcd'[::2]\n", L"ac");
    expect_string_result(L"'abcd'[::-1]\n", L"dcba");
    expect_string_result(L"'abcd'[3:0:-1]\n", L"dcb");
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
    expect_python_error(L"'abc'[False]\n", L"TypeError",
                        L"string indices must be integers or slices");
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

TEST(Interpreter, attribute_assignment_evaluates_rhs_before_target)
{
    test::FileRunner file_runner(L"class Box:\n"
                                 L"    pass\n"
                                 L"a = Box()\n"
                                 L"b = Box()\n"
                                 L"a.value = 1\n"
                                 L"b.value = 2\n"
                                 L"current = a\n"
                                 L"def target():\n"
                                 L"    return current\n"
                                 L"def rhs():\n"
                                 L"    global current\n"
                                 L"    current = b\n"
                                 L"    return 7\n"
                                 L"target().value = rhs()\n"
                                 L"a.value * 10 + b.value\n");

    EXPECT_EQ(Value::from_smi(17), file_runner.return_value);
}

TEST(Interpreter, annotated_attribute_assignment_evaluates_rhs_before_target)
{
    test::FileRunner file_runner(L"class Box:\n"
                                 L"    pass\n"
                                 L"a = Box()\n"
                                 L"b = Box()\n"
                                 L"a.value = 1\n"
                                 L"b.value = 2\n"
                                 L"current = a\n"
                                 L"def target():\n"
                                 L"    return current\n"
                                 L"def rhs():\n"
                                 L"    global current\n"
                                 L"    current = b\n"
                                 L"    return 7\n"
                                 L"target().value: int = rhs()\n"
                                 L"a.value * 10 + b.value\n");

    EXPECT_EQ(Value::from_smi(17), file_runner.return_value);
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
    ASSERT_EQ(1u, function_code->attribute_mutation_caches.size());
    const AttributeMutationInlineCache &cache =
        function_code->attribute_mutation_caches[0];
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
    ASSERT_TRUE(function_code->attribute_read_caches.empty());
    ASSERT_EQ(1u, function_code->attribute_mutation_caches.size());

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

    ASSERT_EQ(1u, function_code->attribute_mutation_caches.size());
    const AttributeMutationInlineCache &cache =
        function_code->attribute_mutation_caches[0];
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

TEST(Interpreter, cached_class_attribute_read_observes_class_write)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    value = 1\n"
                                 L"def get(obj):\n"
                                 L"    return obj.value\n"
                                 L"obj = Cls()\n"
                                 L"first = get(obj)\n"
                                 L"Cls.value = 2\n"
                                 L"get(obj)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
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

TEST(Interpreter, cached_class_chain_attribute_read_observes_secondary_base)
{
    test::FileRunner file_runner(L"class Left:\n"
                                 L"    pass\n"
                                 L"class Right:\n"
                                 L"    value = 1\n"
                                 L"class Derived(Left, Right):\n"
                                 L"    pass\n"
                                 L"def get(obj):\n"
                                 L"    return obj.value\n"
                                 L"obj = Derived()\n"
                                 L"first = get(obj)\n"
                                 L"Right.value = 2\n"
                                 L"get(obj)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
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

TEST(Interpreter, object_class_has_empty_bases_tuple)
{
    test::FileRunner file_runner(L"object.__bases__\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_TRUE(actual.get_ptr<Tuple>()->empty());
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
    dict->set_item(alpha.raw_value(), Value::from_smi(1));
    dict->set_item(removed.raw_value(), Value::from_smi(99));
    dict->set_item(beta.raw_value(), Value::True());
    ASSERT_EQ(Value::None(), dict->del_item(removed.raw_value()));
    expect_method_string(Value::from_oop(dict), dunder_repr_name,
                         L"{'alpha': 1, 'beta': True}");
    expect_method_string(Value::from_oop(dict), dunder_str_name,
                         L"{'alpha': 1, 'beta': True}");

    Dict *reordered_dict = test_context.thread()->make_object_raw<Dict>();
    reordered_dict->set_item(beta.raw_value(), Value::True());
    reordered_dict->set_item(alpha.raw_value(), Value::from_smi(1));
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
    expect_thread_python_error(test_context.thread(), L"StopIteration", L"");

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
    expect_thread_python_error(test_context.thread(), L"StopIteration", L"");

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

TEST(Interpreter, try_finally_runs_cleanup_on_normal_completion)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"try:\n"
                                         L"    result = 1\n"
                                         L"finally:\n"
                                         L"    result = 2\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, try_finally_runs_cleanup_before_reraising)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"result = 0\n"
                                                     L"try:\n"
                                                     L"    raise ValueError\n"
                                                     L"finally:\n"
                                                     L"    result = 2\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    TValue<String> result_name =
        test_context.vm().get_or_create_interned_string_value(L"result");
    EXPECT_EQ(Value::from_smi(2),
              load_global_from_module_for_test(code_obj, result_name));
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

TEST(Interpreter, return_in_finally_overrides_body_exception)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"def f():\n"
                                         L"    try:\n"
                                         L"        raise ValueError\n"
                                         L"    finally:\n"
                                         L"        return 7\n"
                                         L"f()\n");

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, break_in_finally_overrides_body_exception)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"for x in range(3):\n"
                                         L"    try:\n"
                                         L"        raise ValueError\n"
                                         L"    finally:\n"
                                         L"        result = 9\n"
                                         L"        break\n"
                                         L"result + x\n");

    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, continue_in_finally_overrides_body_exception)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"for x in range(3):\n"
                                         L"    result = result + 1\n"
                                         L"    try:\n"
                                         L"        raise ValueError\n"
                                         L"    finally:\n"
                                         L"        continue\n"
                                         L"    result = 99\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(Interpreter, return_through_finally_runs_cleanup_before_return)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"def f():\n"
                                         L"    global result\n"
                                         L"    try:\n"
                                         L"        return 1\n"
                                         L"    finally:\n"
                                         L"        result = 2\n"
                                         L"f() + result * 10\n");

    EXPECT_EQ(Value::from_smi(21), actual);
}

TEST(Interpreter, return_through_finally_that_raises_runs_cleanup_once)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"result = 0\n"
                                  L"def f():\n"
                                  L"    global result\n"
                                  L"    try:\n"
                                  L"        return 1\n"
                                  L"    finally:\n"
                                  L"        result = result + 1\n"
                                  L"        raise ValueError\n"
                                  L"f()\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    TValue<String> result_name =
        test_context.vm().get_or_create_interned_string_value(L"result");
    EXPECT_EQ(Value::from_smi(1),
              load_global_from_module_for_test(code_obj, result_name));
}

TEST(Interpreter, return_in_finally_overrides_protected_return)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"def f():\n"
                                         L"    try:\n"
                                         L"        return 1\n"
                                         L"    finally:\n"
                                         L"        return 2\n"
                                         L"f()\n");

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, break_through_finally_runs_cleanup_before_loop_exit)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"for x in range(3):\n"
                                         L"    try:\n"
                                         L"        break\n"
                                         L"    finally:\n"
                                         L"        result = result + 10\n"
                                         L"result + x\n");

    EXPECT_EQ(Value::from_smi(10), actual);
}

TEST(Interpreter, break_through_inner_finally_does_not_replay_outer_finally)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"try:\n"
                                         L"    for x in range(3):\n"
                                         L"        try:\n"
                                         L"            break\n"
                                         L"        finally:\n"
                                         L"            result = result + 10\n"
                                         L"    result = result + 1\n"
                                         L"finally:\n"
                                         L"    result = result + 100\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(111), actual);
}

TEST(Interpreter, continue_through_finally_runs_cleanup_before_next_iteration)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"for x in range(3):\n"
                                         L"    try:\n"
                                         L"        continue\n"
                                         L"    finally:\n"
                                         L"        result = result + 1\n"
                                         L"    result = 99\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(Interpreter, return_from_else_through_finally_runs_cleanup)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"def f():\n"
                                         L"    global result\n"
                                         L"    try:\n"
                                         L"        pass\n"
                                         L"    except NameError:\n"
                                         L"        pass\n"
                                         L"    else:\n"
                                         L"        return 4\n"
                                         L"    finally:\n"
                                         L"        result = 5\n"
                                         L"f() + result * 10\n");

    EXPECT_EQ(Value::from_smi(54), actual);
}

TEST(Interpreter, return_from_except_through_finally_runs_cleanup)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"def f():\n"
                                         L"    global result\n"
                                         L"    try:\n"
                                         L"        raise NameError\n"
                                         L"    except NameError:\n"
                                         L"        return 6\n"
                                         L"    finally:\n"
                                         L"        result = 7\n"
                                         L"f() + result * 10\n");

    EXPECT_EQ(Value::from_smi(76), actual);
}

TEST(Interpreter, return_through_nested_finally_runs_cleanup_inside_out)
{
    test::VmTestContext test_context;
    Value actual =
        test_context.run_file(L"result = 0\n"
                              L"def f():\n"
                              L"    global result\n"
                              L"    try:\n"
                              L"        try:\n"
                              L"            return 1\n"
                              L"        finally:\n"
                              L"            result = result * 10 + 2\n"
                              L"    finally:\n"
                              L"        result = result * 10 + 3\n"
                              L"f() + result * 10\n");

    EXPECT_EQ(Value::from_smi(231), actual);
}

TEST(Interpreter, try_except_finally_runs_cleanup_after_matched_handler)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"try:\n"
                                         L"    raise NameError\n"
                                         L"except NameError:\n"
                                         L"    result = 1\n"
                                         L"finally:\n"
                                         L"    result = result + 2\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(Interpreter, try_except_finally_runs_cleanup_before_unmatched_reraise)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"result = 0\n"
                                                     L"try:\n"
                                                     L"    raise ValueError\n"
                                                     L"except NameError:\n"
                                                     L"    result = 1\n"
                                                     L"finally:\n"
                                                     L"    result = 2\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    TValue<String> result_name =
        test_context.vm().get_or_create_interned_string_value(L"result");
    EXPECT_EQ(Value::from_smi(2),
              load_global_from_module_for_test(code_obj, result_name));
}

TEST(Interpreter, try_except_finally_runs_cleanup_before_handler_reraise)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"result = 0\n"
                                                     L"try:\n"
                                                     L"    raise NameError\n"
                                                     L"except NameError:\n"
                                                     L"    raise ValueError\n"
                                                     L"finally:\n"
                                                     L"    result = 2\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    TValue<String> result_name =
        test_context.vm().get_or_create_interned_string_value(L"result");
    EXPECT_EQ(Value::from_smi(2),
              load_global_from_module_for_test(code_obj, result_name));
}

TEST(Interpreter, try_except_else_runs_on_body_success)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"try:\n"
                                         L"    result = 1\n"
                                         L"except NameError:\n"
                                         L"    result = 2\n"
                                         L"else:\n"
                                         L"    result = result + 4\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(5), actual);
}

TEST(Interpreter, try_except_else_skips_on_handled_exception)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"try:\n"
                                         L"    raise NameError\n"
                                         L"except NameError:\n"
                                         L"    result = 2\n"
                                         L"else:\n"
                                         L"    result = 99\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, try_except_else_exception_is_not_caught_by_handlers)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"try:\n"
                                                     L"    pass\n"
                                                     L"except ValueError:\n"
                                                     L"    pass\n"
                                                     L"else:\n"
                                                     L"    raise ValueError\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");
}

TEST(Interpreter, try_except_else_finally_runs_cleanup_after_else)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"result = 0\n"
                                         L"try:\n"
                                         L"    result = 1\n"
                                         L"except NameError:\n"
                                         L"    result = 2\n"
                                         L"else:\n"
                                         L"    result = result + 4\n"
                                         L"finally:\n"
                                         L"    result = result + 8\n"
                                         L"result\n");

    EXPECT_EQ(Value::from_smi(13), actual);
}

TEST(Interpreter, try_except_else_finally_cleans_up_else_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"result = 0\n"
                                                     L"try:\n"
                                                     L"    pass\n"
                                                     L"except NameError:\n"
                                                     L"    result = 1\n"
                                                     L"else:\n"
                                                     L"    raise ValueError\n"
                                                     L"finally:\n"
                                                     L"    result = 2\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    TValue<String> result_name =
        test_context.vm().get_or_create_interned_string_value(L"result");
    EXPECT_EQ(Value::from_smi(2),
              load_global_from_module_for_test(code_obj, result_name));
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
    TValue<Exception> exception = make_exception_object(
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

TEST(Interpreter, import_exception_classes_are_builtins)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::True(),
              test_context.run_file(L"ImportError.__mro__[1] is Exception\n"));
    EXPECT_EQ(Value::True(),
              test_context.run_file(
                  L"ModuleNotFoundError.__mro__[1] is ImportError\n"));
    EXPECT_EQ(Value::True(),
              test_context.run_file(
                  L"ModuleNotFoundError.__mro__[2] is Exception\n"));
}

TEST(Interpreter, module_not_found_error_is_caught_by_import_error)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(42),
              test_context.run_file(L"try:\n"
                                    L"    raise ModuleNotFoundError\n"
                                    L"except ImportError:\n"
                                    L"    result = 42\n"
                                    L"result\n"));
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

TEST(Interpreter, user_defined_clover_call_special_name_is_ordinary_function)
{
    test::FileRunner file_runner(
        L"def __clover_call_special__(obj, name, exc_type, message):\n"
        L"    return 123\n"
        L"__clover_call_special__(1, \"__repr__\", TypeError, \"missing\")\n");

    EXPECT_EQ(Value::from_smi(123), file_runner.return_value);
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

TEST(Interpreter, user_defined_clover_globals_name_is_ordinary_function)
{
    test::FileRunner file_runner(L"def __clover_globals__():\n"
                                 L"    return 789\n"
                                 L"__clover_globals__()\n");

    EXPECT_EQ(Value::from_smi(789), file_runner.return_value);
}

TEST(Interpreter, user_code_cannot_use_clover_locals_as_intrinsic)
{
    expect_python_error(L"__clover_locals__()\n", L"NameError",
                        L"name '__clover_locals__' is not defined");
}

TEST(Interpreter, user_defined_clover_locals_name_is_ordinary_function)
{
    test::FileRunner file_runner(L"def __clover_locals__():\n"
                                 L"    return 987\n"
                                 L"__clover_locals__()\n");

    EXPECT_EQ(Value::from_smi(987), file_runner.return_value);
}

TEST(Interpreter, user_code_cannot_use_clover_ternary_pow_as_intrinsic)
{
    expect_python_error(L"__clover_ternary_pow__(2, 3, 5)\n", L"NameError",
                        L"name '__clover_ternary_pow__' is not defined");
}

TEST(Interpreter, user_defined_clover_ternary_pow_name_is_ordinary_function)
{
    test::FileRunner file_runner(L"def __clover_ternary_pow__(a, b, modulo):\n"
                                 L"    return 123\n"
                                 L"__clover_ternary_pow__(2, 3, 5)\n");

    EXPECT_EQ(Value::from_smi(123), file_runner.return_value);
}

TEST(Interpreter, globals_builtin_returns_fresh_slotdict_views)
{
    test::VmTestContext test_context;

    Value actual = test_context.run_file(L"globals()\n");
    ASSERT_TRUE(can_convert_to<SlotDict>(actual));
}

TEST(Interpreter, globals_slotdict_reads_current_module_bindings_only)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(11),
              test_context.run_file(L"x = 11\n"
                                    L"globals()[\"x\"]\n"));
    Value name_value = test_context.run_file(L"globals()[\"__name__\"]\n");
    ASSERT_TRUE(can_convert_to<String>(name_value));
    EXPECT_STREQ(
        L"__main__",
        string_as_wchar_t(TValue<String>::from_value_assumed(name_value)));
    EXPECT_EQ(Value::None(),
              test_context.run_file(L"globals()[\"__doc__\"]\n"));
    EXPECT_EQ(Value::None(),
              test_context.run_file(L"globals()[\"__package__\"]\n"));
    EXPECT_EQ(Value::None(),
              test_context.run_file(L"globals()[\"__loader__\"]\n"));
    EXPECT_EQ(Value::None(),
              test_context.run_file(L"globals()[\"__spec__\"]\n"));
    Value builtins_value =
        test_context.run_file(L"globals()[\"__builtins__\"]\n");
    ASSERT_TRUE(can_convert_to<ModuleObject>(builtins_value));
    expect_python_error(L"globals()[\"len\"]\n", L"KeyError", L"");
}

TEST(Interpreter, main_module_is_inserted_into_sys_modules)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(17),
              test_context.run_file(L"import sys\n"
                                    L"x = 17\n"
                                    L"sys.modules[\"__main__\"].x\n"));
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

TEST(Interpreter, globals_slotdict_len_counts_visible_module_bindings)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(2),
              test_context.run_file(L"globals()[\"x\"] = 1\n"
                                    L"before = len(globals())\n"
                                    L"globals()[\"y\"] = 2\n"
                                    L"len(globals()) - before\n"));
}

TEST(Interpreter, globals_slotdict_repr_is_dict_style)
{
    test::VmTestContext test_context;

    Value actual = test_context.run_file(L"x = 1\n"
                                         L"repr(globals())\n");
    ASSERT_TRUE(can_convert_to<String>(actual));
    std::wstring text =
        string_as_wchar_t(TValue<String>::from_value_assumed(actual));
    EXPECT_EQ(L'{', text.front());
    EXPECT_EQ(L'}', text.back());
    EXPECT_NE(std::wstring::npos, text.find(L"'x': 1"));
}

TEST(Interpreter, globals_slotdict_repr_handles_self_reference)
{
    test::VmTestContext test_context;

    Value actual = test_context.run_file(L"a = globals()\n"
                                         L"repr(a)\n");
    ASSERT_TRUE(can_convert_to<String>(actual));
    std::wstring text =
        string_as_wchar_t(TValue<String>::from_value_assumed(actual));
    EXPECT_NE(std::wstring::npos, text.find(L"'a': {...}"));
}

TEST(Interpreter, globals_slotdict_writes_and_deletes_module_bindings)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(42),
              test_context.run_file(L"globals()[\"written\"] = 42\n"
                                    L"written\n"));

    Value range_value = test_context.run_file(L"globals()[\"range\"] = 42\n"
                                              L"del globals()[\"range\"]\n"
                                              L"range(1)\n");
    ASSERT_TRUE(can_convert_to<RangeIterator>(range_value));
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
    test::VmTestContext test_context;

    Value class_name = test_context.run_file(L"globals().__class__.__name__\n");
    ASSERT_TRUE(can_convert_to<String>(class_name));
    EXPECT_STREQ(
        L"slotdict",
        string_as_wchar_t(TValue<String>::from_value_assumed(class_name)));
    expect_python_error(L"slotdict\n", L"NameError",
                        L"name 'slotdict' is not defined");
}

TEST(Interpreter, locals_builtin_returns_module_slotdict_at_module_scope)
{
    test::VmTestContext test_context;

    Value actual = test_context.run_file(L"locals()\n");
    ASSERT_TRUE(can_convert_to<SlotDict>(actual));
}

TEST(Interpreter, locals_slotdict_reads_and_writes_module_bindings)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(3), test_context.run_file(L"x = 1\n"
                                                        L"locals()[\"y\"] = 2\n"
                                                        L"x + y\n"));
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

TEST(Interpreter, instance_dict_returns_fresh_live_slotdict_views)
{
    test::VmTestContext test_context;

    Value actual = test_context.run_file(L"class C:\n"
                                         L"    pass\n"
                                         L"c = C()\n"
                                         L"c.__dict__\n");
    ASSERT_TRUE(can_convert_to<SlotDict>(actual));

    EXPECT_EQ(Value::False(),
              test_context.run_file(L"class C:\n"
                                    L"    pass\n"
                                    L"c = C()\n"
                                    L"c.__dict__ is c.__dict__\n"));

    EXPECT_EQ(Value::from_smi(42),
              test_context.run_file(L"class C:\n"
                                    L"    pass\n"
                                    L"c = C()\n"
                                    L"c.__dict__[\"x\"] = 42\n"
                                    L"c.x\n"));

    EXPECT_EQ(Value::from_smi(7),
              test_context.run_file(L"class C:\n"
                                    L"    pass\n"
                                    L"c = C()\n"
                                    L"c.x = 7\n"
                                    L"c.__dict__[\"x\"]\n"));
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

TEST(Interpreter, class_assignment_changes_receiver_shape_class)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::True(), test_context.run_file(L"class C:\n"
                                                   L"    pass\n"
                                                   L"class D:\n"
                                                   L"    marker = 42\n"
                                                   L"c = C()\n"
                                                   L"c.__class__ = D\n"
                                                   L"c.__class__ is D\n"));
    EXPECT_EQ(Value::from_smi(42), test_context.run_file(L"class C:\n"
                                                         L"    pass\n"
                                                         L"class D:\n"
                                                         L"    marker = 42\n"
                                                         L"c = C()\n"
                                                         L"c.__class__ = D\n"
                                                         L"c.marker\n"));
}

TEST(Interpreter, class_assignment_rejects_builtin_and_class_object_receivers)
{
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

TEST(Interpreter, module_class_assignment_accepts_module_class)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::True(),
              test_context.run_file(L"m = __builtins__\n"
                                    L"module_class = m.__class__\n"
                                    L"m.__class__ = module_class\n"
                                    L"m.__class__ is module_class\n"));
}

TEST(Interpreter, class_dict_exposes_class_namespace_entries)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(3),
              test_context.run_file(L"class C:\n"
                                    L"    x = 3\n"
                                    L"C.__dict__[\"x\"]\n"));

    EXPECT_EQ(Value::from_smi(9),
              test_context.run_file(L"class C:\n"
                                    L"    pass\n"
                                    L"C.__dict__[\"y\"] = 9\n"
                                    L"C.y\n"));
}

TEST(Interpreter, function_and_module_dicts_are_slotdicts)
{
    test::VmTestContext test_context;

    Value function_dict = test_context.run_file(L"def f():\n"
                                                L"    pass\n"
                                                L"f.__dict__\n");
    ASSERT_TRUE(can_convert_to<SlotDict>(function_dict));

    Value module_dict = test_context.run_file(L"__builtins__.__dict__\n");
    ASSERT_TRUE(can_convert_to<SlotDict>(module_dict));
}

TEST(Interpreter, builtin_container_instances_do_not_expose_dict)
{
    expect_python_error(L"[].__dict__\n", L"AttributeError", L"");
    expect_python_error(L"{}.__dict__\n", L"AttributeError", L"");
    expect_python_error(L"().__dict__\n", L"AttributeError", L"");
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
        EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsImmutableType));
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
        EXPECT_DOUBLE_EQ(expected, result.get_ptr<Float>()->value);
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

TEST(Interpreter, range_builtin_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(5)\n");

    expect_range_iterator(actual, 0, 5, 1);
}

TEST(Interpreter, range_builtin_two_arguments_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(2, 5)\n");

    expect_range_iterator(actual, 2, 5, 1);
}

TEST(Interpreter, range_builtin_three_arguments_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(2, 9, 3)\n");

    expect_range_iterator(actual, 2, 9, 3);
}

TEST(Interpreter, direct_range_for_loop_reports_integer_argument_errors)
{
    expect_python_error(L"for x in range(False):\n"
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

TEST(Interpreter, global_delete_reveals_builtin_after_shadow_delete)
{
    test::FileRunner file_runner(L"range = 42\n"
                                 "del range\n"
                                 "range(1)\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::RangeIterator,
              actual.get_ptr<Object>()->native_layout_id());
}

TEST(Interpreter, range_builtin_requires_integer_argument)
{
    expect_python_error(L"range(False)\n", L"TypeError",
                        L"range() arguments must be integers");
    expect_python_error(L"range(1, False)\n", L"TypeError",
                        L"range() arguments must be integers");
    expect_python_error(L"range(1, 2, False)\n", L"TypeError",
                        L"range() arguments must be integers");
}

TEST(Interpreter, range_integer_argument_error_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    range(False)\n"
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

TEST(Interpreter, python_defined_iter_builtin_calls_dunder_iter)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"iter(range(3))\n");

    expect_range_iterator(actual, 0, 3, 1);
}

TEST(Interpreter, tuple_iter_returns_tuple_iterator)
{
    test::VmTestContext test_context;

    Value iterator_value = test_context.run_file(L"iter((1, 2, 3))\n");
    TupleIterator *iterator =
        CL_ASSERT_CONVERT_TO(TupleIterator, iterator_value);
    EXPECT_EQ(Value::from_smi(3), iterator->length.raw_value());
    EXPECT_EQ(Value::from_smi(0), iterator->index.raw_value());
    Tuple *tuple = iterator->tuple.extract();
    ASSERT_EQ(size_t(3), tuple->size());
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(3), tuple->item_unchecked(2));
}

TEST(Interpreter, python_defined_next_builtin_calls_dunder_next)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"next(range(3))\n");

    EXPECT_EQ(Value::from_smi(0), actual);
}

TEST(Interpreter, python_defined_next_builtin_returns_default_when_exhausted)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"next(iter(()), 42)\n");

    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, python_defined_next_builtin_rejects_multiple_defaults)
{
    expect_python_error(L"next(iter(()), 42, 43)\n", L"TypeError", L"");
}

TEST(Interpreter, tuple_iterator_next_returns_items_until_stop_iteration)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value iterator_value = test_context.run_file(L"iter((4, 5))\n");
    TupleIterator *iterator =
        CL_ASSERT_CONVERT_TO(TupleIterator, iterator_value);
    Tuple *tuple = iterator->tuple.extract();
    expect_tuple_iterator(iterator_value, tuple, 2, 0);

    TValue<Function> next_function = TValue<Function>::from_value_assumed(
        load_builtin_from_module_for_test(test_context, L"next"));

    Value first = test_context.thread()->call_clovervm_function(next_function,
                                                                iterator_value);
    EXPECT_EQ(Value::from_smi(4), first);
    expect_tuple_iterator(iterator_value, tuple, 2, 1);

    Value second = test_context.thread()->call_clovervm_function(
        next_function, iterator_value);
    EXPECT_EQ(Value::from_smi(5), second);
    expect_tuple_iterator(iterator_value, tuple, 2, 2);

    Value exhausted = test_context.thread()->call_clovervm_function(
        next_function, iterator_value);
    EXPECT_TRUE(exhausted.is_exception_marker());
    EXPECT_TRUE(test_context.thread()->has_pending_exception());
    test_context.thread()->clear_pending_exception();
}

TEST(Interpreter, list_iter_returns_list_iterator)
{
    test::VmTestContext test_context;
    Value iterator_value = test_context.run_file(L"iter([1, 2, 3])\n");
    ListIterator *iterator = CL_ASSERT_CONVERT_TO(ListIterator, iterator_value);
    EXPECT_EQ(Value::from_smi(0), iterator->index.raw_value());
    List *list = iterator->list.extract();
    ASSERT_EQ(size_t(3), list->size());
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(3), list->item_unchecked(2));
}

TEST(Interpreter, list_iterator_next_returns_items_until_stop_iteration)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value iterator_value = test_context.run_file(L"iter([4, 5])\n");
    ListIterator *iterator = CL_ASSERT_CONVERT_TO(ListIterator, iterator_value);
    List *list = iterator->list.extract();
    expect_list_iterator(iterator_value, list, 0);

    TValue<Function> next_function = TValue<Function>::from_value_assumed(
        load_builtin_from_module_for_test(test_context, L"next"));

    Value first = test_context.thread()->call_clovervm_function(next_function,
                                                                iterator_value);
    EXPECT_EQ(Value::from_smi(4), first);
    expect_list_iterator(iterator_value, list, 1);

    Value second = test_context.thread()->call_clovervm_function(
        next_function, iterator_value);
    EXPECT_EQ(Value::from_smi(5), second);
    expect_list_iterator(iterator_value, list, 2);

    Value exhausted = test_context.thread()->call_clovervm_function(
        next_function, iterator_value);
    EXPECT_TRUE(exhausted.is_exception_marker());
    EXPECT_TRUE(test_context.thread()->has_pending_exception());
    test_context.thread()->clear_pending_exception();
}

TEST(Interpreter, python_defined_repr_builtin_calls_dunder_repr)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"repr(42)\n");

    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(L"42",
                 string_as_wchar_t(TValue<String>::from_value_assumed(actual)));
}

TEST(Interpreter, python_defined_repr_builtin_formats_float_literals)
{
    test::VmTestContext test_context;

    auto expect_repr = [&](const wchar_t *source, const wchar_t *expected) {
        Value actual = test_context.run_file(source);
        ASSERT_TRUE(can_convert_to<String>(actual));
        EXPECT_STREQ(expected, string_as_wchar_t(
                                   TValue<String>::from_value_assumed(actual)));
    };

    expect_repr(L"repr(1.5)\n", L"1.5");
    expect_repr(L"repr(1.0)\n", L"1.0");
    expect_repr(L"repr(1e20)\n", L"1e+20");
}

TEST(Interpreter, python_defined_len_builtin_calls_dunder_len)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"len(())\n"));
    EXPECT_EQ(Value::from_smi(3), test_context.run_file(L"len((1, 2, 3))\n"));
    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"len([])\n"));
    EXPECT_EQ(Value::from_smi(2), test_context.run_file(L"len([1, 2])\n"));
    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"len({})\n"));
    EXPECT_EQ(Value::from_smi(2),
              test_context.run_file(L"len({'a': 1, 'b': 2})\n"));
    EXPECT_EQ(Value::from_smi(3), test_context.run_file(L"len('abc')\n"));
}

TEST(Interpreter, python_defined_len_builtin_missing_method_error)
{
    expect_python_error(L"len(1)\n", L"TypeError", L"object has no len()");
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

TEST(Interpreter, python_defined_sum_builtin_accumulates_iterable)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(0), test_context.run_file(L"sum(())\n"));
    EXPECT_EQ(Value::from_smi(6), test_context.run_file(L"sum((1, 2, 3))\n"));
    EXPECT_EQ(Value::from_smi(16),
              test_context.run_file(L"sum([1, 2, 3], 10)\n"));
}

TEST(Interpreter, python_defined_any_builtin_tests_iterable_truthiness)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::False(), test_context.run_file(L"any(())\n"));
    EXPECT_EQ(Value::False(),
              test_context.run_file(L"any((0, False, None))\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"any((0, 4, False))\n"));
}

TEST(Interpreter, python_defined_all_builtin_tests_iterable_truthiness)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::True(), test_context.run_file(L"all(())\n"));
    EXPECT_EQ(Value::True(), test_context.run_file(L"all((1, True, 2))\n"));
    EXPECT_EQ(Value::False(), test_context.run_file(L"all((1, 0, True))\n"));
}

TEST(Interpreter, python_defined_min_and_max_builtins_compare_items)
{
    test::VmTestContext test_context;

    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"min((3, 1, 2))\n"));
    EXPECT_EQ(Value::from_smi(1), test_context.run_file(L"min(3, 1, 2)\n"));
    EXPECT_EQ(Value::from_smi(3), test_context.run_file(L"max([3, 1, 2])\n"));
    EXPECT_EQ(Value::from_smi(3), test_context.run_file(L"max(3, 1, 2)\n"));
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

TEST(Interpreter, generic_for_loop_uses_iter_and_next_until_stop_iteration)
{
    test::FileRunner file_runner(L"class Counter:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.i = 0\n"
                                 L"    def __iter__(self):\n"
                                 L"        return self\n"
                                 L"    def __next__(self):\n"
                                 L"        if self.i == 3:\n"
                                 L"            raise StopIteration\n"
                                 L"        value = self.i\n"
                                 L"        self.i += 1\n"
                                 L"        return value\n"
                                 L"total = 0\n"
                                 L"for x in Counter():\n"
                                 L"    total += x\n"
                                 L"total\n");
    EXPECT_EQ(Value::from_smi(3), file_runner.return_value);
}

TEST(Interpreter, for_loop_iterates_over_tuple)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 L"for x in (1, 2, 3):\n"
                                 L"    total += x\n"
                                 L"total\n");

    EXPECT_EQ(Value::from_smi(6), file_runner.return_value);
}

TEST(Interpreter, for_loop_iterates_over_list)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 L"for x in [1, 2, 3]:\n"
                                 L"    total += x\n"
                                 L"total\n");

    EXPECT_EQ(Value::from_smi(6), file_runner.return_value);
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

TEST(Interpreter, with_statement_calls_enter_and_exit)
{
    test::FileRunner file_runner(L"log = 0\n"
                                 L"class Manager:\n"
                                 L"    def __enter__(self):\n"
                                 L"        return 4\n"
                                 L"    def __exit__(self, typ, exc, tb):\n"
                                 L"        global log\n"
                                 L"        log = log + 10\n"
                                 L"        return False\n"
                                 L"with Manager() as value:\n"
                                 L"    log = value + 1\n"
                                 L"log\n");

    EXPECT_EQ(Value::from_smi(15), file_runner.return_value);
}

TEST(Interpreter, with_statement_suppresses_exception_when_exit_returns_true)
{
    test::FileRunner file_runner(L"seen = False\n"
                                 L"class Manager:\n"
                                 L"    def __enter__(self):\n"
                                 L"        return self\n"
                                 L"    def __exit__(self, typ, exc, tb):\n"
                                 L"        global seen\n"
                                 L"        seen = typ is ValueError\n"
                                 L"        return True\n"
                                 L"with Manager():\n"
                                 L"    raise ValueError\n"
                                 L"seen\n");

    EXPECT_EQ(Value::True(), file_runner.return_value);
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

TEST(Interpreter, with_statement_exit_runs_when_as_target_binding_raises)
{
    test::FileRunner file_runner(L"seen = False\n"
                                 L"values = []\n"
                                 L"class Manager:\n"
                                 L"    def __enter__(self):\n"
                                 L"        return 7\n"
                                 L"    def __exit__(self, typ, exc, tb):\n"
                                 L"        global seen\n"
                                 L"        seen = typ is IndexError\n"
                                 L"        return True\n"
                                 L"with Manager() as values[0]:\n"
                                 L"    seen = 99\n"
                                 L"seen\n");

    EXPECT_EQ(Value::True(), file_runner.return_value);
}

TEST(Interpreter, with_statement_exit_runs_before_return)
{
    test::FileRunner file_runner(L"log = 0\n"
                                 L"class Manager:\n"
                                 L"    def __enter__(self):\n"
                                 L"        return self\n"
                                 L"    def __exit__(self, typ, exc, tb):\n"
                                 L"        global log\n"
                                 L"        log = 10\n"
                                 L"        return False\n"
                                 L"def f():\n"
                                 L"    with Manager():\n"
                                 L"        return 7\n"
                                 L"    return 99\n"
                                 L"f() + log\n");

    EXPECT_EQ(Value::from_smi(17), file_runner.return_value);
}

TEST(Interpreter, with_statement_exit_that_raises_during_return_runs_once)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"log = 0\n"
                                  L"class Manager:\n"
                                  L"    def __enter__(self):\n"
                                  L"        return self\n"
                                  L"    def __exit__(self, typ, exc, tb):\n"
                                  L"        global log\n"
                                  L"        log = log + 1\n"
                                  L"        raise ValueError\n"
                                  L"def f():\n"
                                  L"    with Manager():\n"
                                  L"        return 7\n"
                                  L"f()\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    TValue<String> log_name =
        test_context.vm().get_or_create_interned_string_value(L"log");
    EXPECT_EQ(Value::from_smi(1),
              load_global_from_module_for_test(code_obj, log_name));
}

TEST(Interpreter, with_statement_inner_exit_stays_suspended_during_outer_exit)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"log = 0\n"
                                  L"class Outer:\n"
                                  L"    def __enter__(self):\n"
                                  L"        global log\n"
                                  L"        log = log * 10 + 1\n"
                                  L"        return self\n"
                                  L"    def __exit__(self, typ, exc, tb):\n"
                                  L"        global log\n"
                                  L"        log = log * 10 + 2\n"
                                  L"        raise ValueError\n"
                                  L"class Inner:\n"
                                  L"    def __enter__(self):\n"
                                  L"        global log\n"
                                  L"        log = log * 10 + 3\n"
                                  L"        return self\n"
                                  L"    def __exit__(self, typ, exc, tb):\n"
                                  L"        global log\n"
                                  L"        log = log * 10 + 4\n"
                                  L"        return False\n"
                                  L"def f():\n"
                                  L"    with Outer():\n"
                                  L"        with Inner():\n"
                                  L"            return 7\n"
                                  L"f()\n");

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_TRUE(actual.is_exception_marker());
    expect_thread_python_error(test_context.thread(), L"ValueError", L"");

    TValue<String> log_name =
        test_context.vm().get_or_create_interned_string_value(L"log");
    EXPECT_EQ(Value::from_smi(1342),
              load_global_from_module_for_test(code_obj, log_name));
}

TEST(Interpreter, with_statement_multiple_items_exit_in_reverse_order)
{
    test::FileRunner file_runner(L"log = 0\n"
                                 L"class First:\n"
                                 L"    def __enter__(self):\n"
                                 L"        global log\n"
                                 L"        log = log * 10 + 1\n"
                                 L"        return self\n"
                                 L"    def __exit__(self, typ, exc, tb):\n"
                                 L"        global log\n"
                                 L"        log = log * 10 + 2\n"
                                 L"        return False\n"
                                 L"class Second:\n"
                                 L"    def __enter__(self):\n"
                                 L"        global log\n"
                                 L"        log = log * 10 + 3\n"
                                 L"        return self\n"
                                 L"    def __exit__(self, typ, exc, tb):\n"
                                 L"        global log\n"
                                 L"        log = log * 10 + 4\n"
                                 L"        return False\n"
                                 L"with First(), Second():\n"
                                 L"    log = log * 10 + 5\n"
                                 L"log\n");

    EXPECT_EQ(Value::from_smi(13542), file_runner.return_value);
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

TEST(Interpreter, left_shift_overflow_smi)
{
    expect_python_error(L"1 << 58\n", L"OverflowError", L"integer overflow");
}

TEST(Interpreter, left_shift_overflow_register)
{
    expect_python_error(L"a = 1\n"
                        L"b = 58\n"
                        L"a << b\n",
                        L"OverflowError", L"integer overflow");
}

TEST(Interpreter, add_overflow)
{
    expect_python_error(L"288230376151711743 + 1\n", L"OverflowError",
                        L"integer overflow");
}

TEST(Interpreter, subtract_overflow)
{
    expect_python_error(L"-288230376151711743 - 2\n", L"OverflowError",
                        L"integer overflow");
}

TEST(Interpreter, multiply_overflow)
{
    expect_python_error(L"288230376151711743 * 2\n", L"OverflowError",
                        L"integer overflow");
}

TEST(Interpreter, negate_overflow)
{
    Value expected = Value::from_smi(kMinSmi);
    test::FileRunner file_runner(L"-288230376151711743 - 1\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);

    expect_python_error(L"x = -288230376151711743 - 1\n"
                        L"-x\n",
                        L"OverflowError", L"integer overflow");
}
