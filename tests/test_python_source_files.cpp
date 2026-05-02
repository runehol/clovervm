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
}  // namespace

TEST_P(PythonSourceFileTest, RunsSelfCheckingSource)
{
    std::wstring source = read_source_file(GetParam().path);
    (void)cl::test::FileRunner(source.c_str());
}

INSTANTIATE_TEST_SUITE_P(SelfCheckingPythonFiles, PythonSourceFileTest,
                         testing::ValuesIn(python_source_files()),
                         source_file_test_name);
