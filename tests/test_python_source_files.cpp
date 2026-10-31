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

TEST(PythonSourceFiles, AugmentedArithmeticSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/augmented_arithmetic.py",
                           "tests/python/augmented_arithmetic_protocol.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, OperatorInplaceSurvivesEverySafepointReclamation)
{
    std::wstring source =
        read_source_file("tests/python/test_operator_inplace.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(PythonSourceFiles, BinasciiHexSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/test_binascii_hex.py",
                           "tests/python/test_binascii_hex_limits.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, StringPaddingSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/string_padding.py",
                           "tests/python/string_padding_limits.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, BytesSplittingSurvivesEverySafepointReclamation)
{
    std::wstring source = read_source_file("tests/python/bytes_split.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(PythonSourceFiles, StringSeparatorSearchSurvivesEverySafepointReclamation)
{
    std::wstring source =
        read_source_file("tests/python/string_separator_search.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(PythonSourceFiles, BytesJoinSurvivesEverySafepointReclamation)
{
    for(const char *path:
        {"tests/python/bytes_join.py", "tests/python/bytes_join_limits.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, StringJoinSurvivesEverySafepointReclamation)
{
    for(const char *path:
        {"tests/python/str_join.py", "tests/python/str_join_limits.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, IntegerBaseFormattingSurvivesEverySafepointReclamation)
{
    std::wstring source = read_source_file("tests/python/builtin_int_bases.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(PythonSourceFiles, HeapqMergeSurvivesEverySafepointReclamation)
{
    std::wstring source = read_source_file("tests/python/heapq_merge.py");
    cl::test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
}

TEST(PythonSourceFiles, BinasciiCRCSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/test_binascii_crc.py",
                           "tests/python/test_binascii_crc_limits.py"})
    {
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker())
            << path;
    }
}

TEST(PythonSourceFiles, Base64BytearraySurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/test_base64_bytearray.py",
                           "tests/python/test_base64_bytearray_mutation.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, ItertoolsExpansionSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/itertools_selection.py",
                           "tests/python/itertools_pairwise.py",
                           "tests/python/itertools_count.py",
                           "tests/python/itertools_count_mixed_numbers.py",
                           "tests/python/itertools_count_reentry.py",
                           "tests/python/itertools_accumulate.py",
                           "tests/python/itertools_batched.py",
                           "tests/python/itertools_zip_longest.py",
                           "tests/python/itertools_product.py",
                           "tests/python/itertools_combinatorial.py"})
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
        {"tests/python/file_io.py", "tests/python/file_io_subclasses.py",
         "tests/python/file_io_readinto.py"})
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
    for(const char *path:
        {"tests/python/io_base.py", "tests/python/io_context_exit.py",
         "tests/python/io_capability_checks.py"})
    {
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, RawIOReadsSurviveEverySafepointReclamation)
{
    for(const char *path:
        {"tests/python/rawio_read.py", "tests/python/rawio_read_validation.py",
         "tests/python/rawio_read_callbacks.py",
         "tests/python/rawio_readall.py",
         "tests/python/rawio_readall_errors.py",
         "tests/python/rawio_read_resized_buffer.py",
         "tests/python/file_io_readall.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, BufferedIOBaseSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/buffered_io_base.py",
                           "tests/python/buffered_io_readinto.py",
                           "tests/python/buffered_io_mro.py",
                           "tests/python/buffered_io_resized_buffer.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, BufferedReaderSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/buffered_reader.py",
                           "tests/python/buffered_reader_nonblocking.py",
                           "tests/python/buffered_reader_position.py",
                           "tests/python/buffered_reader_callbacks.py",
                           "tests/python/buffered_reader_errors.py",
                           "tests/python/buffered_reader_reinitialize.py",
                           "tests/python/buffered_reader_lifecycle.py",
                           "tests/python/buffered_reader_reentry.py",
                           "tests/python/buffered_reader_mixed.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(PythonSourceFiles, BufferedWriterSurvivesEverySafepointReclamation)
{
    for(const char *path: {"tests/python/buffered_writer.py",
                           "tests/python/buffered_writer_nonblocking.py",
                           "tests/python/buffered_writer_zero.py",
                           "tests/python/buffered_writer_callbacks.py",
                           "tests/python/buffered_writer_position.py",
                           "tests/python/buffered_writer_lifecycle.py",
                           "tests/python/buffered_writer_reentry.py",
                           "tests/python/buffered_writer_mixed.py",
                           "tests/python/buffered_writer_boundaries.py",
                           "tests/python/buffered_writer_reinitialize.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(TextCodecReclamation, OneShotConversionAndErrors)
{
    for(const char *path:
        {"tests/python/text_codec.py", "tests/python/text_codec_errors.py",
         "tests/python/text_codec_calls.py",
         "tests/python/text_codec_options.py",
         "tests/python/text_codec_boundaries.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
}

TEST(UnicodeErrorReclamation, ConstructorAndOverflowAttributes)
{
    for(const char *path: {"tests/python/unicode_error.py",
                           "tests/python/unicode_error_attributes.py",
                           "tests/python/unicode_error_callbacks.py"})
    {
        SCOPED_TRACE(path);
        std::wstring source = read_source_file(path);
        cl::test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(source.c_str()).is_exception_marker());
    }
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
        PythonSourceFile{"tests/python/tuple_hash.py"},
        PythonSourceFile{"tests/python/tuple_hash_callbacks.py"},
        PythonSourceFile{"tests/python/sequence_ordering_subclasses.py"}),
    source_file_test_name);

INSTANTIATE_TEST_SUITE_P(SelfCheckingPythonFiles, PythonSourceFileTest,
                         testing::ValuesIn(python_source_files()),
                         source_file_test_name);
