#include "test_helpers.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    struct PythonSourceFile
    {
        std::filesystem::path path;
    };

    void PrintTo(const PythonSourceFile &file, std::ostream *out)
    {
        *out << file.path.string();
    }

    std::wstring read_source_file(const std::filesystem::path &path)
    {
        std::ifstream stream(path);
        if(!stream)
        {
            throw std::runtime_error("Could not open Python source file: " +
                                     path.string());
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        std::string bytes = buffer.str();
        return std::wstring(bytes.begin(), bytes.end());
    }

    std::vector<PythonSourceFile> python_source_files()
    {
        std::vector<PythonSourceFile> files;
        std::filesystem::path root("tests/python");
        for(const std::filesystem::directory_entry &entry:
            std::filesystem::directory_iterator(root))
        {
            if(entry.is_regular_file() && entry.path().extension() == ".py")
            {
                files.push_back({entry.path()});
            }
        }
        std::sort(
            files.begin(), files.end(),
            [](const PythonSourceFile &left, const PythonSourceFile &right) {
                return left.path.string() < right.path.string();
            });
        return files;
    }

    std::string
    source_file_test_name(const testing::TestParamInfo<PythonSourceFile> &info)
    {
        std::string name = info.param.path.stem().string();
        for(char &ch: name)
        {
            if(!std::isalnum(static_cast<unsigned char>(ch)))
            {
                ch = '_';
            }
        }
        return name;
    }

    class PythonSourceFileTest : public testing::TestWithParam<PythonSourceFile>
    {
    };

    class TupleSubclassReclamationTest
        : public testing::TestWithParam<PythonSourceFile>
    {
    };
}  // namespace

TEST_P(PythonSourceFileTest, RunsSelfCheckingSource)
{
    std::wstring source = read_source_file(GetParam().path);
    cl::test::FileRunner runner(source.c_str());
    EXPECT_FALSE(runner.return_value.is_exception_marker())
        << "self-checking Python source raised an exception";
}

TEST(PythonSourceFiles, IntegerBaseFormattingSurvivesEverySafepointReclamation)
{
    std::wstring source = read_source_file("tests/python/builtin_int_bases.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(PythonSourceFiles, ItertoolsExpansionSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/itertools_selection.py",
                           "tests/python/itertools_pairwise.py",
                           "tests/python/itertools_count.py",
                           "tests/python/itertools_count_mixed_numbers.py",
                           "tests/python/itertools_count_reentry.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker())
            << "itertools state must stay rooted across input and callbacks";
    }
}

TEST(PythonSourceFiles, IntTrueDivisionSurvivesEverySafepointReclamation)
{
    std::wstring source = read_source_file("tests/python/int_true_division.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(PythonSourceFiles, FileIOSurvivesEverySafepointReclamation)
{
    for(const char *path:
        {"tests/python/file_io.py", "tests/python/file_io_subclasses.py"})
    {
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, IOBaseSurvivesEverySafepointReclamation)
{
    std::wstring source = read_source_file("tests/python/io_base.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(OSErrorReclamation, ConstructorAndLatentAttributes)
{
    for(const char *path:
        {"tests/python/os_error.py", "tests/python/os_error_latent.py",
         "tests/python/os_error_presence.py",
         "tests/python/os_error_callbacks.py",
         "tests/python/os_error_progress.py",
         "tests/python/os_errno_bridge.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST_P(TupleSubclassReclamationTest, RunsSelfCheckingSource)
{
    std::wstring source = read_source_file(GetParam().path);
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker())
        << "tuple integration source raised during forced reclamation";
}

INSTANTIATE_TEST_SUITE_P(
    TupleIntegrationFiles, TupleSubclassReclamationTest,
    testing::Values(
        PythonSourceFile{"tests/python/test_json_tuple_rows.py"},
        PythonSourceFile{"tests/python/tuple_subclass_integration.py"},
        PythonSourceFile{"tests/python/sequence_ordering_subclasses.py"}),
    source_file_test_name);

INSTANTIATE_TEST_SUITE_P(SelfCheckingPythonFiles, PythonSourceFileTest,
                         testing::ValuesIn(python_source_files()),
                         source_file_test_name);
