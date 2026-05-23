#include "dict.h"
#include "import_system.h"
#include "list.h"
#include "module_object.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>

using namespace cl;

namespace
{
    class TemporaryImportRoot
    {
    public:
        TemporaryImportRoot()
            : path(std::filesystem::temp_directory_path() /
                   std::filesystem::path(L"clovervm-import-test"))
        {
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
        }

        ~TemporaryImportRoot() { std::filesystem::remove_all(path); }

        void write_file(const std::filesystem::path &relative_path)
        {
            write_file(relative_path, "# test module\n");
        }

        void write_file(const std::filesystem::path &relative_path,
                        const std::string &source)
        {
            std::filesystem::path file_path = path / relative_path;
            std::filesystem::create_directories(file_path.parent_path());
            std::ofstream stream(file_path);
            stream << source;
        }

        std::filesystem::path path;
    };

    void replace_sys_path(test::VmTestContext &context, List *path)
    {
        TValue<String> path_name =
            context.vm().get_or_create_interned_string_value(L"path");
        bool stored = context.vm().sys_module().extract()->set_own_property(
            path_name, Value::from_oop(path));
        ASSERT_TRUE(stored);
    }

    List *make_sys_path(test::VmTestContext &context)
    {
        return context.thread()->make_object_raw<List>();
    }

    TValue<String> module_name(test::VmTestContext &context,
                               const wchar_t *name)
    {
        return context.vm().get_or_create_interned_string_value(name);
    }

    std::wstring value_as_wstring(Value value)
    {
        return string_as_wchar_t(TValue<String>::from_value_assumed(value));
    }

    std::wstring value_as_wstring(TValue<String> value)
    {
        return string_as_wchar_t(value);
    }

    Value module_attr(test::VmTestContext &context, ModuleObject *module,
                      const wchar_t *name)
    {
        return module->get_own_property(module_name(context, name));
    }

    void use_source_tree_python_path(test::VmTestContext &context)
    {
        List *path = make_sys_path(context);
        path->append(module_name(context, L"tests/python").raw_value());
        replace_sys_path(context, path);
    }
}  // namespace

TEST(ImportSystem, SourceFinderFindsModuleFileOnSysPath)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"sample.py");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    std::optional<ModuleSpec> spec = find_source_module_spec(
        context.thread(), module_name(context, L"sample"));
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(L"sample", spec->name);
    EXPECT_FALSE(spec->is_package);
    EXPECT_EQ((root.path / L"sample.py").lexically_normal().wstring(),
              spec->origin);
    EXPECT_TRUE(spec->submodule_search_locations.empty());
}

TEST(ImportSystem, SourceFinderFindsPackageInitBeforeModuleFile)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"sample.py");
    root.write_file(L"sample/__init__.py");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    std::optional<ModuleSpec> spec = find_source_module_spec(
        context.thread(), module_name(context, L"sample"));
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(L"sample", spec->name);
    EXPECT_TRUE(spec->is_package);
    EXPECT_EQ(
        (root.path / L"sample" / L"__init__.py").lexically_normal().wstring(),
        spec->origin);
    ASSERT_EQ(1u, spec->submodule_search_locations.size());
    EXPECT_EQ((root.path / L"sample").lexically_normal().wstring(),
              spec->submodule_search_locations[0]);
}

TEST(ImportSystem, SourceFinderIgnoresNonStringSysPathEntries)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"sample.py");

    List *path = make_sys_path(context);
    path->append(Value::from_smi(1));
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    std::optional<ModuleSpec> spec = find_source_module_spec(
        context.thread(), module_name(context, L"sample"));
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ((root.path / L"sample.py").lexically_normal().wstring(),
              spec->origin);
}

TEST(ImportSystem, SourceFinderReturnsEmptyForMissesAndDottedNames)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    EXPECT_FALSE(find_source_module_spec(context.thread(),
                                         module_name(context, L"missing"))
                     .has_value());
    EXPECT_FALSE(find_source_module_spec(context.thread(),
                                         module_name(context, L"pkg.mod"))
                     .has_value());
}

TEST(ImportSystem, ImportModuleAbsoluteLoadsExistingSourceTreeModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    TValue<String> name = module_name(context, L"assignment");
    Value imported = import_module_absolute(context.thread(), name);
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *module = imported.get_ptr<ModuleObject>();

    EXPECT_EQ(imported, context.vm().imported_modules().extract()->get_item(
                            name.raw_value()));
    EXPECT_EQ(L"assignment",
              value_as_wstring(module_attr(context, module, L"__name__")));
    EXPECT_EQ(Value::None(), module_attr(context, module, L"__doc__"));
    EXPECT_EQ(L"",
              value_as_wstring(module_attr(context, module, L"__package__")));
    EXPECT_EQ(Value::None(), module_attr(context, module, L"__loader__"));
    EXPECT_EQ(Value::None(), module_attr(context, module, L"__spec__"));
    EXPECT_EQ(context.vm().global_builtins_module().raw_value(),
              module_attr(context, module, L"__builtins__"));
    EXPECT_EQ(std::filesystem::absolute("tests/python/assignment.py")
                  .lexically_normal()
                  .wstring(),
              value_as_wstring(module_attr(context, module, L"__file__")));
    EXPECT_EQ(Value::from_smi(3), module_attr(context, module, L"marker"));
}

TEST(ImportSystem, ImportModuleAbsoluteReturnsExistingSysModulesValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    TValue<String> name = module_name(context, L"assignment");
    context.vm().imported_modules().extract()->set_item(name.raw_value(),
                                                        Value::from_smi(77));

    EXPECT_EQ(Value::from_smi(77),
              import_module_absolute(context.thread(), name));
}

TEST(ImportSystem, ImportModuleAbsoluteInstallsPackagePath)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "value = 12\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value imported =
        import_module_absolute(context.thread(), module_name(context, L"pkg"));
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *module = imported.get_ptr<ModuleObject>();
    EXPECT_EQ(L"pkg",
              value_as_wstring(module_attr(context, module, L"__package__")));
    Value package_path = module_attr(context, module, L"__path__");
    ASSERT_TRUE(can_convert_to<List>(package_path));
    List *package_path_list = package_path.get_ptr<List>();
    ASSERT_EQ(1u, package_path_list->size());
    EXPECT_EQ((root.path / L"pkg").lexically_normal().wstring(),
              value_as_wstring(package_path_list->item_unchecked(0)));
    EXPECT_EQ(Value::from_smi(12), module_attr(context, module, L"value"));
}

TEST(ImportSystem, ImportModuleAbsoluteUsesCachedModuleWithoutReexecution)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    TValue<String> name = module_name(context, L"assignment");
    Value first = import_module_absolute(context.thread(), name);
    ASSERT_TRUE(can_convert_to<ModuleObject>(first));
    ModuleObject *module = first.get_ptr<ModuleObject>();
    ASSERT_TRUE(module->set_own_property(module_name(context, L"marker"),
                                         Value::from_smi(99)));

    Value second = import_module_absolute(context.thread(), name);
    EXPECT_EQ(first, second);
    EXPECT_EQ(Value::from_smi(99), module_attr(context, module, L"marker"));
}

TEST(ImportSystem, ImportModuleAbsoluteRaisesModuleNotFoundForMiss)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    TValue<String> name = module_name(context, L"notpresent");
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ModuleNotFoundError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"No module named 'notpresent'",
              value_as_wstring(exception.extract()->message.value()));
    EXPECT_FALSE(
        context.vm().imported_modules().extract()->contains(name.raw_value()));
}

TEST(ImportSystem, ImportModuleAbsoluteRemovesModuleOnExecutionFailure)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"broken.py", "raise ValueError\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    TValue<String> name = module_name(context, L"broken");
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ValueError"),
              context.thread()
                  ->pending_exception_object()
                  .extract()
                  ->get_shape()
                  ->get_class());
    EXPECT_FALSE(
        context.vm().imported_modules().extract()->contains(name.raw_value()));
}

TEST(ImportSystem, ImportModuleAbsoluteRemovesModuleOnCompileFailure)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"broken.py", "def nope(:\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    TValue<String> name = module_name(context, L"broken");
    EXPECT_THROW((void)import_module_absolute(context.thread(), name),
                 std::exception);
    EXPECT_FALSE(
        context.vm().imported_modules().extract()->contains(name.raw_value()));
}
