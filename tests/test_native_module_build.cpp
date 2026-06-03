#include "build_config.h"
#include "builtin_types/dict.h"
#include "builtin_types/float.h"
#include "builtin_types/list.h"
#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "import_system/import_system.h"
#include "import_system/module_finder.h"
#include "object_model/function.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>

using namespace cl;

TEST(NativeModuleBuild, TestModuleBuildsIntoBuildStdlib)
{
    std::filesystem::path module_path =
        std::filesystem::path(CL_BUILD_STDLIB_DIR) /
        (std::wstring(L"_test_native") + CL_NATIVE_MODULE_SUFFIX);

    EXPECT_TRUE(std::filesystem::is_regular_file(module_path))
        << module_path.string();
}

TEST(NativeModuleBuild, FinderDiscoversTestModuleAsNativeExtension)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    std::optional<ModuleSpec> spec =
        find_module_spec(context.thread(), L"_test_native", L"_test_native",
                         sys_path(context.thread()));
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(ModuleSpecKind::NativeExtension, spec->kind);
    EXPECT_EQ(L"_test_native", spec->name);
    EXPECT_FALSE(spec->is_package);
    EXPECT_EQ((std::filesystem::path(CL_BUILD_STDLIB_DIR) /
               (std::wstring(L"_test_native") + CL_NATIVE_MODULE_SUFFIX))
                  .lexically_normal()
                  .wstring(),
              spec->origin);
    EXPECT_TRUE(spec->submodule_search_locations.empty());
}

TEST(NativeModuleBuild, ImportingNativeExtensionPopulatesModuleGlobals)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"_test_native");
    Value imported = import_module_absolute(context.thread(), name);
    ASSERT_FALSE(imported.is_exception_marker());
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *module = imported.get_ptr<ModuleObject>();

    TValue<String> answer_name =
        context.vm().get_or_create_interned_string_value(L"answer");
    EXPECT_EQ(Value::from_smi(42), module->get_own_property(answer_name));
    TValue<String> greeting_name =
        context.vm().get_or_create_interned_string_value(L"greeting");
    Value greeting = module->get_own_property(greeting_name);
    ASSERT_TRUE(can_convert_to<String>(greeting));
    EXPECT_EQ(std::wstring(L"hello \u03bb"),
              std::wstring(
                  string_view(TValue<String>::from_value_assumed(greeting))));
    TValue<String> nothing_name =
        context.vm().get_or_create_interned_string_value(L"nothing");
    EXPECT_EQ(Value::None(), module->get_own_property(nothing_name));
    TValue<String> answer_func_name =
        context.vm().get_or_create_interned_string_value(L"answer_func");
    Value answer_func = module->get_own_property(answer_func_name);
    ASSERT_TRUE(can_convert_to<Function>(answer_func));
    Optional<TValue<String>> answer_func_docstring =
        assume_convert_to<Function>(answer_func)->docstring.value();
    ASSERT_TRUE(answer_func_docstring.has_value());
    EXPECT_EQ(context.vm().get_or_create_interned_string_value(L"Return 42."),
              answer_func_docstring.value());
    EXPECT_EQ(Value::from_smi(42),
              context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(answer_func)));

    TValue<String> identity_func_name =
        context.vm().get_or_create_interned_string_value(L"identity_func");
    Value identity_func = module->get_own_property(identity_func_name);
    ASSERT_TRUE(can_convert_to<Function>(identity_func));
    EXPECT_EQ(Value::from_smi(123),
              context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(identity_func),
                  Value::from_smi(123)));

    TValue<String> double_constant_name =
        context.vm().get_or_create_interned_string_value(L"double_constant");
    Value double_constant = module->get_own_property(double_constant_name);
    ASSERT_TRUE(can_convert_to<Function>(double_constant));
    Value double_constant_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(double_constant));
    ASSERT_TRUE(can_convert_to<Float>(double_constant_result));
    EXPECT_DOUBLE_EQ(1.5, double_constant_result.get_ptr<Float>()->value);

    TValue<String> float_plus_one_name =
        context.vm().get_or_create_interned_string_value(L"float_plus_one");
    Value float_plus_one = module->get_own_property(float_plus_one_name);
    ASSERT_TRUE(can_convert_to<Function>(float_plus_one));
    Value smi_float_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(float_plus_one),
        Value::from_smi(2));
    ASSERT_TRUE(can_convert_to<Float>(smi_float_result));
    EXPECT_DOUBLE_EQ(3.0, smi_float_result.get_ptr<Float>()->value);
    Value float_float_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(float_plus_one),
        context.thread()->make_object_value<Float>(2.5).raw_value());
    ASSERT_TRUE(can_convert_to<Float>(float_float_result));
    EXPECT_DOUBLE_EQ(3.5, float_float_result.get_ptr<Float>()->value);
    Value non_float_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(float_plus_one), Value::None());
    EXPECT_TRUE(non_float_result.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(
        L"value cannot be converted to float",
        std::wstring(string_as_wchar_t(exception.extract()->message.value())));
    context.thread()->clear_pending_exception();

    TValue<String> tuple2_name =
        context.vm().get_or_create_interned_string_value(L"tuple2");
    Value tuple2 = module->get_own_property(tuple2_name);
    ASSERT_TRUE(can_convert_to<Function>(tuple2));
    Value tuple2_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(tuple2), Value::from_smi(5),
        Value::from_smi(8));
    ASSERT_TRUE(can_convert_to<Tuple>(tuple2_result));
    Tuple *tuple2_object = tuple2_result.get_ptr<Tuple>();
    ASSERT_EQ(2u, tuple2_object->size());
    EXPECT_EQ(Value::from_smi(5), tuple2_object->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(8), tuple2_object->item_unchecked(1));

    TValue<String> tuple3_name =
        context.vm().get_or_create_interned_string_value(L"tuple3");
    Value tuple3 = module->get_own_property(tuple3_name);
    ASSERT_TRUE(can_convert_to<Function>(tuple3));
    Value tuple3_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(tuple3), Value::from_smi(1),
        Value::from_smi(2), Value::from_smi(3));
    ASSERT_TRUE(can_convert_to<Tuple>(tuple3_result));
    Tuple *tuple3_object = tuple3_result.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple3_object->size());
    EXPECT_EQ(Value::from_smi(1), tuple3_object->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple3_object->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(3), tuple3_object->item_unchecked(2));

    TValue<String> empty_tuple_name =
        context.vm().get_or_create_interned_string_value(L"empty_tuple");
    Value empty_tuple = module->get_own_property(empty_tuple_name);
    ASSERT_TRUE(can_convert_to<Function>(empty_tuple));
    Value empty_tuple_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(empty_tuple));
    ASSERT_TRUE(can_convert_to<Tuple>(empty_tuple_result));
    EXPECT_TRUE(empty_tuple_result.get_ptr<Tuple>()->empty());

    TValue<String> bad_tuple_name =
        context.vm().get_or_create_interned_string_value(L"bad_tuple");
    Value bad_tuple = module->get_own_property(bad_tuple_name);
    ASSERT_TRUE(can_convert_to<Function>(bad_tuple));
    Value bad_tuple_result = context.thread()->call_clovervm_function(
        TValue<Function>::from_value_assumed(bad_tuple));
    EXPECT_TRUE(bad_tuple_result.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> bad_tuple_exception =
        context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ValueError"),
              bad_tuple_exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"native extension tuple items must not be null",
              std::wstring(string_as_wchar_t(
                  bad_tuple_exception.extract()->message.value())));
    context.thread()->clear_pending_exception();

    auto expect_sum_result = [&](const wchar_t *function_name, double expected,
                                 auto... args) {
        TValue<String> sum_name =
            context.vm().get_or_create_interned_string_value(function_name);
        Value sum = module->get_own_property(sum_name);
        ASSERT_TRUE(can_convert_to<Function>(sum));
        Value sum_result = context.thread()->call_clovervm_function(
            TValue<Function>::from_value_assumed(sum), args...);
        ASSERT_TRUE(can_convert_to<Float>(sum_result));
        EXPECT_DOUBLE_EQ(expected, sum_result.get_ptr<Float>()->value);
    };
    expect_sum_result(L"sum2", 3.0, Value::from_smi(1), Value::from_smi(2));
    expect_sum_result(L"sum3", 6.0, Value::from_smi(1), Value::from_smi(2),
                      Value::from_smi(3));
    expect_sum_result(L"sum4", 10.0, Value::from_smi(1), Value::from_smi(2),
                      Value::from_smi(3), Value::from_smi(4));
    expect_sum_result(L"sum5", 15.0, Value::from_smi(1), Value::from_smi(2),
                      Value::from_smi(3), Value::from_smi(4),
                      Value::from_smi(5));
    expect_sum_result(L"sum6", 21.0, Value::from_smi(1), Value::from_smi(2),
                      Value::from_smi(3), Value::from_smi(4),
                      Value::from_smi(5), Value::from_smi(6));
    expect_sum_result(L"sum7", 28.0, Value::from_smi(1), Value::from_smi(2),
                      Value::from_smi(3), Value::from_smi(4),
                      Value::from_smi(5), Value::from_smi(6),
                      Value::from_smi(7));

    EXPECT_EQ(imported, context.vm().imported_modules().extract()->get_item(
                            name.raw_value()));
}

TEST(NativeModuleBuild, TimeWrapperImportsNativeExtensionFunctions)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value result = context.run_file(L"import time\n"
                                    L"before = time.monotonic()\n"
                                    L"wall = time.time()\n"
                                    L"time.sleep(0)\n"
                                    L"after = time.monotonic()\n"
                                    L"result = after >= before and wall > 0.0\n"
                                    L"result\n");
    EXPECT_EQ(Value::True(), result);
}

TEST(NativeModuleBuild, TimeSleepRejectsNegativeValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value result = context.run_file(L"import time\n"
                                    L"time.sleep(-14)\n");
    EXPECT_TRUE(result.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ValueError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(
        L"sleep length must be non-negative",
        std::wstring(string_as_wchar_t(exception.extract()->message.value())));
}

TEST(NativeModuleBuild, TimeFormatsEpochTuples)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value asctime_result =
        context.run_file(L"import time\n"
                         L"epoch = time.gmtime(0)\n"
                         L"result = len(time.asctime(epoch))\n"
                         L"result\n");
    EXPECT_EQ(Value::from_smi(24), asctime_result);

    Value strftime_result = context.run_file(
        L"import time\n"
        L"epoch = time.gmtime(0)\n"
        L"result = len(time.strftime(\"%Y-%m-%d %H:%M:%S\", epoch))\n"
        L"result\n");
    EXPECT_EQ(Value::from_smi(19), strftime_result);
}

TEST(NativeModuleBuild, NativeMathModuleExportsSqrt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value result = context.run_file(L"import _math\n"
                                    L"result = _math.sqrt(3) > 1.7 and "
                                    L"_math.sqrt(3) < 1.8 and "
                                    L"_math.sqrt(4.0) == 2.0\n"
                                    L"result\n");
    EXPECT_EQ(Value::True(), result);
}

TEST(NativeModuleBuild, NativeMathSqrtReportsErrors)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value negative_result = context.run_file(L"import _math\n"
                                             L"_math.sqrt(-1.0)\n");
    EXPECT_TRUE(negative_result.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> negative_exception =
        context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ValueError"),
              negative_exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"math domain error",
              std::wstring(string_as_wchar_t(
                  negative_exception.extract()->message.value())));
    context.thread()->clear_pending_exception();

    Value type_result = context.run_file(L"import _math\n"
                                         L"_math.sqrt(None)\n");
    EXPECT_TRUE(type_result.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> type_exception =
        context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
              type_exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"value cannot be converted to float",
              std::wstring(string_as_wchar_t(
                  type_exception.extract()->message.value())));
}

TEST(NativeModuleBuild, StdlibMathWrapperUsesTrustedSqrt)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value result = context.run_file(L"import math\n"
                                    L"result = math.sqrt(3) > 1.7 and "
                                    L"math.sqrt(3) < 1.8 and "
                                    L"math.sqrt(4.0) == 2.0\n"
                                    L"result\n");
    EXPECT_EQ(Value::True(), result);
}

TEST(NativeModuleBuild, StdlibMathSqrtReportsTrustedErrors)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value negative_result = context.run_file(L"import math\n"
                                             L"math.sqrt(-1.0)\n");
    EXPECT_TRUE(negative_result.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> negative_exception =
        context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ValueError"),
              negative_exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"math domain error",
              std::wstring(string_as_wchar_t(
                  negative_exception.extract()->message.value())));
    context.thread()->clear_pending_exception();

    Value type_result = context.run_file(L"import math\n"
                                         L"math.sqrt(None)\n");
    EXPECT_TRUE(type_result.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> type_exception =
        context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
              type_exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"value cannot be converted to float",
              std::wstring(string_as_wchar_t(
                  type_exception.extract()->message.value())));
}

static void
expect_native_import_error_and_uncached(test::VmTestContext &context,
                                        const wchar_t *module_name,
                                        const wchar_t *expected_message)
{
    TValue<String> name =
        context.vm().get_or_create_interned_string_value(module_name);
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ImportError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(expected_message, std::wstring(string_as_wchar_t(
                                    exception.extract()->message.value())));
    EXPECT_FALSE(
        context.vm().imported_modules().extract()->contains(name.raw_value()));
}

TEST(NativeModuleBuild, ImportingNativeExtensionWithoutInitSymbolRaises)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name = context.vm().get_or_create_interned_string_value(
        L"_test_native_missing_symbol");
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ImportError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(
        L"native module '_test_native_missing_symbol' does not export "
        L"'clover_module_init__test_native_missing_symbol'",
        std::wstring(string_as_wchar_t(exception.extract()->message.value())));
    EXPECT_FALSE(
        context.vm().imported_modules().extract()->contains(name.raw_value()));
}

TEST(NativeModuleBuild, AddValuePropagatesExceptionMarker)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name = context.vm().get_or_create_interned_string_value(
        L"_test_native_bad_value");
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"OverflowError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(
        L"integer is outside the supported native API range",
        std::wstring(string_as_wchar_t(exception.extract()->message.value())));
    EXPECT_FALSE(
        context.vm().imported_modules().extract()->contains(name.raw_value()));
}

TEST(NativeModuleBuild, InitFailureWithoutExceptionRaisesImportError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    expect_native_import_error_and_uncached(
        context, L"_test_native_fail_no_exception",
        L"native module init failed without setting an exception for "
        L"'_test_native_fail_no_exception'");
}
