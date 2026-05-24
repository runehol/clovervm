#include "build_config.h"
#include "dict.h"
#include "exception_object.h"
#include "float.h"
#include "function.h"
#include "import_system.h"
#include "list.h"
#include "module_finder.h"
#include "module_object.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
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

TEST(NativeModuleBuild, BuilderFailureRemovesModuleFromSysModules)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    expect_native_import_error_and_uncached(
        context, L"_test_native_bad_constant",
        L"native module int constant name must be non-empty");
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
