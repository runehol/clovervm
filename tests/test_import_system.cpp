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
            std::filesystem::path file_path = path / relative_path;
            std::filesystem::create_directories(file_path.parent_path());
            std::ofstream stream(file_path);
            stream << "# test module\n";
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
