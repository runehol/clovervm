#include "build_config.h"
#include "dict.h"
#include "exception_object.h"
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
    EXPECT_EQ(imported, context.vm().imported_modules().extract()->get_item(
                            name.raw_value()));
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
