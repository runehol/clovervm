#include "builtin_types/dict.h"
#include "builtin_types/list.h"
#include "builtin_types/module_loader_object.h"
#include "builtin_types/module_object.h"
#include "builtin_types/module_spec_object.h"
#include "builtin_types/str.h"
#include "import_system/import_system.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
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

    Value sys_modules_get(test::VmTestContext &context, TValue<String> name)
    {
        return context.vm()
            .imported_modules()
            .extract()
            ->get_item_for_str(context.thread(), name)
            .value();
    }

    void sys_modules_set(test::VmTestContext &context, TValue<String> name,
                         Value value)
    {
        ASSERT_FALSE(context.vm()
                         .imported_modules()
                         .extract()
                         ->set_item_for_str(context.thread(), name, value)
                         .has_exception());
    }

    bool sys_modules_contains(test::VmTestContext &context, TValue<String> name)
    {
        return context.vm()
            .imported_modules()
            .extract()
            ->contains_for_str(context.thread(), name)
            .value();
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

TEST(ImportSystem, SourceFinderFindsNamespacePackageDirectory)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    std::filesystem::create_directories(root.path / L"sample");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    std::optional<ModuleSpec> spec = find_source_module_spec(
        context.thread(), module_name(context, L"sample"));
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(ModuleSpecKind::Namespace, spec->kind);
    EXPECT_EQ(L"sample", spec->name);
    EXPECT_TRUE(spec->is_package);
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

    EXPECT_EQ(imported, sys_modules_get(context, name));
    EXPECT_EQ(L"assignment",
              value_as_wstring(module_attr(context, module, L"__name__")));
    EXPECT_EQ(Value::None(), module_attr(context, module, L"__doc__"));
    EXPECT_EQ(L"",
              value_as_wstring(module_attr(context, module, L"__package__")));
    Value loader = module_attr(context, module, L"__loader__");
    ASSERT_TRUE(can_convert_to<ModuleLoaderObject>(loader));
    EXPECT_EQ(L"source",
              value_as_wstring(loader.get_ptr<ModuleLoaderObject>()->kind));
    Value spec = module_attr(context, module, L"__spec__");
    ASSERT_TRUE(can_convert_to<ModuleSpecObject>(spec));
    EXPECT_EQ(loader, spec.get_ptr<ModuleSpecObject>()->loader);
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
    sys_modules_set(context, name, Value::from_smi(77));

    EXPECT_EQ(Value::from_smi(77),
              import_module_absolute(context.thread(), name));
}

TEST(ImportSystem, ImportModuleAbsoluteRaisesWhenSysModulesValueIsNone)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    TValue<String> name = module_name(context, L"assignment");
    sys_modules_set(context, name, Value::None());

    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ModuleNotFoundError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"No module named 'assignment'",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, ImportModuleAbsoluteReturnsSysModulesReplacementAfterExec)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"replacement.py", "import sys\n"
                                       "sys.modules[\"replacement\"] = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    TValue<String> name = module_name(context, L"replacement");
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_EQ(Value::from_smi(42), imported);
    EXPECT_EQ(Value::from_smi(42), sys_modules_get(context, name));
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

TEST(ImportSystem, ImportModuleAbsoluteAllowsSelfImportDuringExecution)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"self_import.py", "started = 1\n"
                                       "import self_import\n"
                                       "seen_during_import = "
                                       "self_import.started\n"
                                       "started = 2\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value imported = import_module_absolute(
        context.thread(), module_name(context, L"self_import"));
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *module = imported.get_ptr<ModuleObject>();
    EXPECT_EQ(Value::from_smi(1),
              module_attr(context, module, L"seen_during_import"));
    EXPECT_EQ(Value::from_smi(2), module_attr(context, module, L"started"));
}

TEST(ImportSystem, ImportModuleAbsoluteAllowsSelfStarImportDuringExecution)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"self_import.py", "started = 1\n"
                                       "from self_import import *\n"
                                       "seen_during_import = started\n"
                                       "started = 2\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value imported = import_module_absolute(
        context.thread(), module_name(context, L"self_import"));
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *module = imported.get_ptr<ModuleObject>();
    EXPECT_EQ(Value::from_smi(1),
              module_attr(context, module, L"seen_during_import"));
    EXPECT_EQ(Value::from_smi(2), module_attr(context, module, L"started"));
}

TEST(ImportSystem, ImportModuleAbsoluteLoadsDottedModuleAndBindsParentAttribute)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "package_marker = 7\n");
    root.write_file(L"pkg/mod.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value imported = import_module_absolute(context.thread(),
                                            module_name(context, L"pkg.mod"));
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *child = imported.get_ptr<ModuleObject>();
    EXPECT_EQ(L"pkg.mod",
              value_as_wstring(module_attr(context, child, L"__name__")));
    EXPECT_EQ(L"pkg",
              value_as_wstring(module_attr(context, child, L"__package__")));
    EXPECT_EQ(Value::from_smi(42), module_attr(context, child, L"value"));

    Value parent_value = sys_modules_get(context, module_name(context, L"pkg"));
    ASSERT_TRUE(can_convert_to<ModuleObject>(parent_value));
    ModuleObject *parent = parent_value.get_ptr<ModuleObject>();
    EXPECT_EQ(imported, module_attr(context, parent, L"mod"));
    EXPECT_EQ(imported,
              sys_modules_get(context, module_name(context, L"pkg.mod")));
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
    EXPECT_FALSE(sys_modules_contains(context, name));
}

TEST(ImportSystem, ImportModuleAbsoluteRaisesWhenParentIsNotPackage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg.py", "value = 1\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    TValue<String> name = module_name(context, L"pkg.mod");
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ModuleNotFoundError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"No module named 'pkg.mod'; 'pkg' is not a package",
              value_as_wstring(exception.extract()->message.value()));
    EXPECT_TRUE(sys_modules_contains(context, module_name(context, L"pkg")));
    EXPECT_FALSE(
        sys_modules_contains(context, module_name(context, L"pkg.mod")));
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
    EXPECT_FALSE(sys_modules_contains(context, name));
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
    Value imported = import_module_absolute(context.thread(), name);
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(L"SyntaxError",
              value_as_wstring(
                  exception.extract()->get_shape()->get_class()->get_name()));
    EXPECT_FALSE(sys_modules_contains(context, name));
}

TEST(ImportSystem, BuiltinImportLoadsAbsoluteModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"__import__('assignment')\n");
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *module = imported.get_ptr<ModuleObject>();
    EXPECT_EQ(L"assignment",
              value_as_wstring(module_attr(context, module, L"__name__")));
    EXPECT_EQ(Value::from_smi(3), module_attr(context, module, L"marker"));
}

TEST(ImportSystem, BuiltinImportUsesDefaultsAndReturnsCachedValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    TValue<String> name = module_name(context, L"assignment");
    sys_modules_set(context, name, Value::from_smi(55));

    EXPECT_EQ(Value::from_smi(55),
              context.run_file(L"__import__('assignment')\n"));
}

TEST(ImportSystem, ImportSysAfterDeletingSysModulesEntryReloadsBuiltinModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value actual = context.run_file(L"import sys\n"
                                    L"original = sys\n"
                                    L"del sys.modules[\"sys\"]\n"
                                    L"del sys\n"
                                    L"import sys\n"
                                    L"sys is original and "
                                    L"sys.modules[\"sys\"] is sys\n");
    EXPECT_EQ(Value::True(), actual);
}

TEST(ImportSystem,
     ImportBuiltinsAfterDeletingSysModulesEntryReloadsBuiltinModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value actual = context.run_file(L"import sys\n"
                                    L"import builtins\n"
                                    L"original = builtins\n"
                                    L"del sys.modules[\"builtins\"]\n"
                                    L"del builtins\n"
                                    L"import builtins\n"
                                    L"builtins is original and "
                                    L"sys.modules[\"builtins\"] is builtins\n");
    EXPECT_EQ(Value::True(), actual);
}

TEST(ImportSystem, ModuleReprFormatsBuiltinModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value actual = context.run_file(L"import sys\n"
                                    L"repr(sys)\n");
    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_EQ(L"<module 'sys' (built-in)>", value_as_wstring(actual));
}

TEST(ImportSystem, BuiltinModuleExposesSpecAndLoader)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value actual =
        context.run_file(L"import sys\n"
                         L"sys.__spec__.name == \"sys\" and "
                         L"sys.__spec__.origin == \"built-in\" and "
                         L"sys.__spec__.loader is sys.__loader__ and "
                         L"sys.__spec__.loader.kind == \"builtin\" and "
                         L"sys.__spec__.loader.name is None and "
                         L"sys.__spec__.loader.path is None and "
                         L"sys.__spec__.submodule_search_locations "
                         L"is None and "
                         L"not sys.__spec__.has_location and "
                         L"sys.__spec__.parent == \"\" and "
                         L"sys.__spec__.__dict__[\"name\"] == \"sys\" and "
                         L"sys.__loader__.__dict__[\"kind\"] == \"builtin\"\n");
    EXPECT_EQ(Value::True(), actual);
}

TEST(ImportSystem, BuiltinImportOfDottedModuleReturnsTopLevelPackage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "package_marker = 7\n");
    root.write_file(L"pkg/mod.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value imported = context.run_file(L"__import__('pkg.mod')\n");
    ASSERT_TRUE(can_convert_to<ModuleObject>(imported));
    ModuleObject *parent = imported.get_ptr<ModuleObject>();
    EXPECT_EQ(L"pkg",
              value_as_wstring(module_attr(context, parent, L"__name__")));
    Value child = module_attr(context, parent, L"mod");
    ASSERT_TRUE(can_convert_to<ModuleObject>(child));
    EXPECT_EQ(Value::from_smi(42),
              module_attr(context, child.get_ptr<ModuleObject>(), L"value"));
}

TEST(ImportSystem, BuiltinImportRejectsNonStringName)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value imported = context.run_file(L"__import__(1)\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"__import__ name must be str",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, BuiltinImportRelativeImportWithoutPackageRaisesImportError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Value imported =
        context.run_file(L"__import__('assignment', None, None, (), 1)\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ImportError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"attempted relative import with no known parent package",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, ImportStatementLoadsModuleAndStoresBinding)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value marker = context.run_file(L"import assignment\n"
                                    L"assignment.marker\n");
    EXPECT_EQ(Value::from_smi(3), marker);
}

TEST(ImportSystem, ImportStatementLoadsDottedModuleAndStoresTopLevelBinding)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "package_marker = 7\n");
    root.write_file(L"pkg/mod.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value marker = context.run_file(L"import pkg.mod\n"
                                    L"pkg.mod.value\n");
    EXPECT_EQ(Value::from_smi(42), marker);
}

TEST(ImportSystem, ModuleReprFormatsSourceModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"mod.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"import mod\n"
                                    L"repr(mod)\n");
    ASSERT_TRUE(can_convert_to<String>(actual));
    std::wstring expected = L"<module 'mod' from '";
    expected += (root.path / L"mod.py").lexically_normal().wstring();
    expected += L"'>";
    EXPECT_EQ(expected, value_as_wstring(actual));
}

TEST(ImportSystem, SourceModuleExposesSpecAndLoader)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"mod.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual =
        context.run_file(L"import mod\n"
                         L"mod.__spec__.name == \"mod\" and "
                         L"mod.__spec__.origin == mod.__file__ and "
                         L"mod.__spec__.loader is mod.__loader__ and "
                         L"mod.__loader__.kind == \"source\" and "
                         L"mod.__loader__.name == \"mod\" and "
                         L"mod.__loader__.path == mod.__file__ and "
                         L"mod.__spec__.submodule_search_locations is None and "
                         L"mod.__spec__.has_location and "
                         L"mod.__spec__.parent == \"\"\n");
    EXPECT_EQ(Value::True(), actual);
}

TEST(ImportSystem, SourcePackageExposesSpecSearchLocations)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(
        L"import pkg\n"
        L"pkg.__spec__.name == \"pkg\" and "
        L"pkg.__spec__.origin == pkg.__file__ and "
        L"pkg.__spec__.loader is pkg.__loader__ and "
        L"pkg.__loader__.kind == \"source\" and "
        L"pkg.__loader__.name == \"pkg\" and "
        L"pkg.__loader__.path == pkg.__file__ and "
        L"pkg.__spec__.submodule_search_locations[0] == pkg.__path__[0] and "
        L"pkg.__spec__.has_location and "
        L"pkg.__spec__.parent == \"pkg\"\n");
    EXPECT_EQ(Value::True(), actual);
}

TEST(ImportSystem, ImportStatementSupportsMultipleImportsAndAliases)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "package_marker = 7\n");
    root.write_file(L"pkg/mod.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    path->append(module_name(context, L"tests/python").raw_value());
    replace_sys_path(context, path);

    Value actual =
        context.run_file(L"import assignment as a, pkg.mod as alias\n"
                         L"a.marker + alias.value\n");
    EXPECT_EQ(Value::from_smi(45), actual);
}

TEST(ImportSystem, ImportStatementExplicitAliasToHeadStoresLeafModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "package_marker = 7\n");
    root.write_file(L"pkg/mod.py", "value = 42\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"import pkg.mod as pkg\n"
                                    L"pkg.value\n");
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(ImportSystem, ImportStatementUsesParentPackagePathForChildLookup)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    std::filesystem::path sysroot = root.path / L"sysroot";
    std::filesystem::path childroot = root.path / L"childroot";
    std::filesystem::create_directories(sysroot / L"pkg");
    std::filesystem::create_directories(childroot);
    root.write_file(L"sysroot/pkg/__init__.py",
                    "__path__ = [r'" + childroot.string() + "']\n");
    root.write_file(L"childroot/mod.py", "value = 99\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, sysroot.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value marker = context.run_file(L"import pkg.mod\n"
                                    L"pkg.mod.value\n");
    EXPECT_EQ(Value::from_smi(99), marker);
}

TEST(ImportSystem, ImportStatementLoadsDottedModuleThroughNamespacePackages)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"tests/python/arithmetic.py", "value = 37\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"import tests.python.arithmetic\n"
                                    L"tests.python.arithmetic.value\n");
    EXPECT_EQ(Value::from_smi(37), actual);
}

TEST(ImportSystem, ModuleReprFormatsNamespacePackage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"tests/python/arithmetic.py", "value = 37\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"import tests.python.arithmetic\n"
                                    L"repr(tests)\n");
    ASSERT_TRUE(can_convert_to<String>(actual));
    std::wstring expected = L"<module 'tests' (namespace) from ['";
    expected += (root.path / L"tests").lexically_normal().wstring();
    expected += L"']>";
    EXPECT_EQ(expected, value_as_wstring(actual));
}

TEST(ImportSystem, NamespacePackageExposesSpecAndLoader)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"tests/python/arithmetic.py", "value = 37\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual =
        context.run_file(L"import tests.python.arithmetic\n"
                         L"tests.__spec__.name == \"tests\" and "
                         L"tests.__spec__.origin == \"namespace\" and "
                         L"tests.__spec__.loader is tests.__loader__ and "
                         L"tests.__loader__.kind == \"namespace\" and "
                         L"tests.__loader__.name == \"tests\" and "
                         L"tests.__loader__.path is None and "
                         L"tests.__spec__.submodule_search_locations[0] == "
                         L"tests.__path__[0] and "
                         L"not tests.__spec__.has_location and "
                         L"tests.__spec__.parent == \"tests\"\n");
    EXPECT_EQ(Value::True(), actual);
}

TEST(ImportSystem, FromImportLoadsModuleThroughNamespacePackages)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"tests/python/arithmetic.py", "value = 38\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"from tests.python import arithmetic\n"
                                    L"arithmetic.value\n");
    EXPECT_EQ(Value::from_smi(38), actual);
}

TEST(ImportSystem, ImportStatementStoresLocalBindingInFunction)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value marker = context.run_file(L"def f():\n"
                                    L"    import assignment\n"
                                    L"    return assignment.marker\n"
                                    L"f()\n");
    EXPECT_EQ(Value::from_smi(3), marker);
}

TEST(ImportSystem, ImportStatementIgnoresGlobalImportBinding)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value marker = context.run_file(L"def __import__(name, globals, locals, "
                                    L"fromlist, level):\n"
                                    L"    return 99\n"
                                    L"import assignment\n"
                                    L"assignment.marker\n");
    EXPECT_EQ(Value::from_smi(3), marker);
}

TEST(ImportSystem, ImportStatementUsesBuiltinsImportBinding)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"def fake(name, globals, locals, "
                                      L"fromlist, level):\n"
                                      L"    return 42\n"
                                      L"__builtins__.__import__ = fake\n"
                                      L"import assignment\n"
                                      L"assignment\n");
    EXPECT_EQ(Value::from_smi(42), imported);
}

TEST(ImportSystem, ImportStatementPropagatesBuiltinsImportException)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"def fake(name, globals, locals, "
                                      L"fromlist, level):\n"
                                      L"    raise ValueError\n"
                                      L"__builtins__.__import__ = fake\n"
                                      L"import assignment\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ValueError"),
              exception.extract()->get_shape()->get_class());
}

TEST(ImportSystem, ImportStatementRejectsNonModuleBuiltinsBinding)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"__builtins__ = \"boom\"\n"
                                      L"import assignment\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"__builtins__ must be a module",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, FromImportStatementLoadsNames)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value actual = context.run_file(L"from assignment import marker, value\n"
                                    L"marker + value\n");
    EXPECT_EQ(Value::from_smi(10), actual);
}

TEST(ImportSystem, FromImportStatementSupportsAliases)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value actual =
        context.run_file(L"from assignment import marker as m, value\n"
                         L"m + value\n");
    EXPECT_EQ(Value::from_smi(10), actual);
}

TEST(ImportSystem, FromImportStatementStoresLocalBindingInFunction)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value actual = context.run_file(L"def f():\n"
                                    L"    from assignment import marker\n"
                                    L"    return marker\n"
                                    L"f()\n");
    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(ImportSystem, FromImportStatementHonorsGlobalDeclaration)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value actual = context.run_file(L"def f():\n"
                                    L"    global marker\n"
                                    L"    from assignment import marker\n"
                                    L"f()\n"
                                    L"marker\n");
    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(ImportSystem, FromImportStatementAliasHonorsGlobalDeclaration)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value actual = context.run_file(L"def f():\n"
                                    L"    global m\n"
                                    L"    from assignment import marker as m\n"
                                    L"f()\n"
                                    L"m\n");
    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(ImportSystem, FromImportMissingModuleRaisesModuleNotFound)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"from notpresent import marker\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ModuleNotFoundError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"No module named 'notpresent'",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, FromImportMissingNameRaisesImportError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"from assignment import notpresent\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ImportError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"cannot import name 'notpresent' from 'assignment'",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, FromImportPassesFullFromlistToImportHook)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value actual = context.run_file(
        L"original = __builtins__.__import__\n"
        L"def fake(name, globals, locals, fromlist, level):\n"
        L"    global seen\n"
        L"    seen = fromlist\n"
        L"    return original(name, globals, locals, fromlist, level)\n"
        L"__builtins__.__import__ = fake\n"
        L"from assignment import marker, value\n"
        L"seen[0] == \"marker\" and seen[1] == \"value\"\n");
    EXPECT_EQ(Value::True(), actual);
}

TEST(ImportSystem, FromImportNonModuleHookReturnRaisesImportError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"def fake(name, globals, locals, "
                                      L"fromlist, level):\n"
                                      L"    return 42\n"
                                      L"__builtins__.__import__ = fake\n"
                                      L"from assignment import marker\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ImportError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"cannot import name 'marker' from '<unknown>'",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, FromImportStarLoadsPublicNamesWithoutAll)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"mod.py", "public = 7\n_private = 9\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"from mod import *\n"
                                    L"hidden = 0\n"
                                    L"try:\n"
                                    L"    _private\n"
                                    L"except NameError:\n"
                                    L"    hidden = 1\n"
                                    L"public * 10 + hidden\n");
    EXPECT_EQ(Value::from_smi(71), actual);
}

TEST(ImportSystem, FromImportStarUsesAll)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"mod.py", "__all__ = (\"_private\",)\n"
                               "_private = 11\n"
                               "public = 7\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"from mod import *\n"
                                    L"hidden = 0\n"
                                    L"try:\n"
                                    L"    public\n"
                                    L"except NameError:\n"
                                    L"    hidden = 1\n"
                                    L"_private * 10 + hidden\n");
    EXPECT_EQ(Value::from_smi(111), actual);
}

TEST(ImportSystem, FromImportStarAllCanImportPackageSubmodule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "__all__ = (\"child\",)\n");
    root.write_file(L"pkg/child.py", "value = 12\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"from pkg import *\n"
                                    L"child.value\n");
    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(ImportSystem, FromImportStarRejectsBadAllItem)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"mod.py", "__all__ = (1,)\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value imported = context.run_file(L"from mod import *\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"Item in mod.__all__ must be str, not int",
              value_as_wstring(exception.extract()->message.value()));
}

TEST(ImportSystem, FromImportStarPassesStarFromlistToImportHook)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value actual = context.run_file(
        L"original = __builtins__.__import__\n"
        L"def fake(name, globals, locals, fromlist, level):\n"
        L"    global seen\n"
        L"    seen = fromlist\n"
        L"    return original(name, globals, locals, fromlist, level)\n"
        L"__builtins__.__import__ = fake\n"
        L"from assignment import *\n"
        L"seen[0]\n");
    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_EQ(L"*", value_as_wstring(actual));
}

TEST(ImportSystem, FromImportStarInFunctionIsRejectedByCodegen)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    test::expect_compile_python_error(context,
                                      L"def f():\n"
                                      L"    from assignment import "
                                      L"*\n",
                                      L"SyntaxError",
                                      L"import * only allowed at module level");
}

TEST(ImportSystem, RelativeFromImportLoadsSiblingModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "from . import sibling\n"
                                        "value = sibling.value\n");
    root.write_file(L"pkg/sibling.py", "value = 12\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"import pkg\n"
                                    L"pkg.value\n");
    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(ImportSystem, RelativeFromImportLoadsSiblingModuleAttribute)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "from .sibling import value\n");
    root.write_file(L"pkg/sibling.py", "value = 13\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"import pkg\n"
                                    L"pkg.value\n");
    EXPECT_EQ(Value::from_smi(13), actual);
}

TEST(ImportSystem, RelativeFromImportCanClimbToParentPackage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TemporaryImportRoot root;
    root.write_file(L"pkg/__init__.py", "root_value = 1\n");
    root.write_file(L"pkg/sibling.py", "value = 14\n");
    root.write_file(L"pkg/sub/__init__.py", "");
    root.write_file(L"pkg/sub/mod.py", "from .. import sibling\n"
                                       "value = sibling.value\n");

    List *path = make_sys_path(context);
    path->append(module_name(context, root.path.wstring().c_str()).raw_value());
    replace_sys_path(context, path);

    Value actual = context.run_file(L"import pkg.sub.mod\n"
                                    L"pkg.sub.mod.value\n");
    EXPECT_EQ(Value::from_smi(14), actual);
}

TEST(ImportSystem, RelativeFromImportWithoutPackageRaisesImportError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    use_source_tree_python_path(context);

    Value imported = context.run_file(L"from .assignment import marker\n");
    EXPECT_TRUE(imported.is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object,
              context.thread()->pending_exception_kind());
    TValue<Exception> exception = context.thread()->pending_exception_object();
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"ImportError"),
              exception.extract()->get_shape()->get_class());
    EXPECT_EQ(L"attempted relative import with no known parent package",
              value_as_wstring(exception.extract()->message.value()));
}
