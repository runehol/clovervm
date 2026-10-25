#include "builtin_types/file_io.h"
#include "memory/native_layout_descriptor.h"
#include "platform/file_io.h"
#include "test_helpers.h"
#include <cerrno>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <string>
#include <unistd.h>

using namespace cl;

namespace
{
    class TestFd
    {
    public:
        explicit TestFd(int value) : fd(value) {}
        ~TestFd()
        {
            if(fd >= 0)
                (void)close(fd);
        }
        int release()
        {
            int result = fd;
            fd = -1;
            return result;
        }
        int fd;
    };

    int temporary_fd()
    {
        char path[] = "/tmp/clover-fileio-XXXXXX";
        int fd = mkstemp(path);
        if(fd >= 0)
            (void)unlink(path);
        return fd;
    }

    Value run(test::VmTestContext &context, const std::wstring &source)
    {
        return context.run_file((L"import io\n" + source).c_str());
    }
}  // namespace

TEST(FileIO, SharedLayoutHasOnlyFourPythonSlotsAndNoCleanupHook)
{
    EXPECT_EQ(sizeof(InlineSlotObject), CL_OFFSETOF(FileIOObject, name));
    EXPECT_EQ(sizeof(InlineSlotObject) + 4 * sizeof(Value),
              CL_OFFSETOF(FileIOObject, fd));
    const auto &release = release_descriptor_for(NativeLayoutId::FileIO);
    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(InlineSlotObject::native_static_release_count() + 4,
              release.static_release_count);
    EXPECT_EQ(nullptr, release.custom_dealloc);
    EXPECT_EQ(sizeof(FileIOObject),
              object_size_descriptor_for(NativeLayoutId::FileIO)
                  .static_size_in_bytes);
    EXPECT_TRUE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::FileIO, 4));
    EXPECT_FALSE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::FileIO, 5));
}

TEST(FileIO, BorrowingAndOwningDescriptors)
{
    TestFd fd(temporary_fd());
    ASSERT_GE(fd.fd, 0);
    test::VmTestContext context;
    std::wstring number = std::to_wstring(fd.fd);
    EXPECT_EQ(
        Value::True(),
        run(context, L"import io\nio.f = io.FileIO(" + number +
                         L", 'r+', closefd=False)\n"
                         L"assert io.f.fileno() == " +
                         number +
                         L"\nassert not io.f.closefd\nio.f.close()\nassert "
                         L"io.f.closed\nTrue\n"));
    EXPECT_GE(fcntl(fd.fd, F_GETFD), 0);
    int owned = fd.release();
    EXPECT_EQ(Value::True(),
              run(context, L"io.f = io.FileIO(" + number +
                               L", 'r+')\n"
                               L"assert io.f.closefd\nio.f.close()\nassert "
                               L"io.f.closed\nio.f.close()\nTrue\n"));
    errno = 0;
    EXPECT_EQ(-1, fcntl(owned, F_GETFD));
    EXPECT_EQ(EBADF, errno);
}

TEST(FileIO, FailedConstructionPreservesCallerDescriptorAndRollsBackNewOpen)
{
    TestFd directory(open(".", O_RDONLY));
    ASSERT_GE(directory.fd, 0);
    test::VmTestContext context;
    EXPECT_EQ(Value::True(),
              run(context, L"import io\ntry:\n    io.FileIO(" +
                               std::to_wstring(directory.fd) +
                               L")\n    assert False\nexcept IsADirectoryError "
                               L"as e:\n    assert e.filename == " +
                               std::to_wstring(directory.fd) + L"\nTrue\n"));
    EXPECT_GE(fcntl(directory.fd, F_GETFD), 0);
    int candidate = dup(directory.fd);
    ASSERT_GE(candidate, 0);
    ASSERT_EQ(0, close(candidate));
    EXPECT_EQ(Value::True(),
              run(context,
                  L"try:\n    io.FileIO('.')\n    assert False\nexcept "
                  L"IsADirectoryError:\n    pass\nTrue\n"));
    EXPECT_EQ(-1, fcntl(candidate, F_GETFD));
}

TEST(FileIO, CloseFailureDetachesAndCannotCloseReusedDescriptor)
{
    TestFd fd(temporary_fd());
    TestFd replacement(temporary_fd());
    ASSERT_GE(fd.fd, 0);
    ASSERT_GE(replacement.fd, 0);
    test::VmTestContext context;
    EXPECT_EQ(Value::None(),
              run(context, L"import io\nio.f = io.FileIO(" +
                               std::to_wstring(fd.fd) + L")\nNone\n"));
    int number = fd.release();
    ASSERT_EQ(0, close(number));
    EXPECT_EQ(
        Value::True(),
        run(context,
            L"try:\n    io.f.close()\n    assert False\nexcept OSError as "
            L"e:\n    assert e.errno == 9\nassert io.f.closed\nTrue\n"));
    TestFd reused(dup2(replacement.fd, number));
    ASSERT_EQ(number, reused.fd);
    EXPECT_EQ(Value::None(), run(context, L"io.f.close()\n"));
    EXPECT_GE(fcntl(reused.fd, F_GETFD), 0);
}

TEST(FileIO, AbandonmentDoesNotCloseAtReclamationOrVMTeardown)
{
    TestFd fd(temporary_fd());
    ASSERT_GE(fd.fd, 0);
    {
        test::VmTestContext context;
        EXPECT_EQ(Value::None(), run(context, L"import io\nio.f = io.FileIO(" +
                                                  std::to_wstring(fd.fd) +
                                                  L")\nio.f = None\n"));
        context.vm().run_heap_reclamation();
        EXPECT_GE(fcntl(fd.fd, F_GETFD), 0);
        // Also leave a reachable owning stream for the VM teardown case.
        EXPECT_EQ(Value::None(),
                  run(context, L"io.f = io.FileIO(" + std::to_wstring(fd.fd) +
                                   L")\nNone\n"));
    }
    EXPECT_GE(fcntl(fd.fd, F_GETFD), 0);
}

TEST(FileIO, NonblockingAndShortPipeTransfers)
{
    int pipes[2];
    ASSERT_EQ(0, pipe(pipes));
    TestFd read_end(pipes[0]);
    TestFd write_end(pipes[1]);
    ASSERT_EQ(0, fcntl(read_end.fd, F_SETFL, O_NONBLOCK));
    ASSERT_EQ(0, fcntl(write_end.fd, F_SETFL, O_NONBLOCK));
    test::VmTestContext context;
    EXPECT_EQ(
        Value::True(),
        run(context,
            L"import io\nio.r = io.FileIO(" + std::to_wstring(read_end.fd) +
                L", closefd=False)\n"
                L"io.w = io.FileIO(" +
                std::to_wstring(write_end.fd) +
                L", 'w', closefd=False)\n"
                L"assert io.r.read(8) is None\nassert not io.r.seekable()\n"
                L"assert io.w.write(b'abc') == 3\nassert io.r.read(8) == "
                L"b'abc'\n"
                L"try:\n    io.r.seek(0)\n    assert False\nexcept OSError:\n  "
                L"  "
                L"pass\nTrue\n"));
    char fill[4096] = {};
    while(write(write_end.fd, fill, sizeof(fill)) > 0)
    {
    }
    ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
    EXPECT_EQ(Value::True(),
              run(context, L"assert io.w.write(b'x') is None\nTrue\n"));
    // Empty the pipe, then request more than its capacity in one raw write.
    char drain[65536];
    while(read(read_end.fd, drain, sizeof(drain)) > 0)
    {
    }
    EXPECT_EQ(Value::True(),
              run(context,
                  L"data = b'x'\nfor i in range(20):\n    data = data + "
                  L"data\nn = io.w.write(data)\nassert "
                  L"0 < n < len(data)\nio.r.close()\nio.w.close()\nTrue\n"));
    EXPECT_GE(fcntl(read_end.fd, F_GETFD), 0);
    EXPECT_GE(fcntl(write_end.fd, F_GETFD), 0);
}

TEST(FileIO, PathOpenIsNonInheritable)
{
    test::VmTestContext context;
    Value fd =
        run(context,
            L"import io\nio.f = io.FileIO('CMakeLists.txt')\nio.f.fileno()\n");
    ASSERT_TRUE(fd.is_smi());
    EXPECT_NE(0, fcntl(static_cast<int>(fd.get_smi()), F_GETFD) & FD_CLOEXEC);
    EXPECT_EQ(Value::None(), run(context, L"io.f.close()\n"));
}

TEST(FileIO, UsesHostSignalPolicyAndReportsBrokenPipeWhenIgnored)
{
    // Set policy only in the test child. FileIO and VM construction must not
    // install a signal handler or introduce per-write masking.
    ASSERT_EXIT(
        {
            if(signal(SIGPIPE, SIG_DFL) == SIG_ERR)
                _exit(1);
            {
                test::VmTestContext context;
                struct sigaction action;
                if(sigaction(SIGPIPE, nullptr, &action) != 0 ||
                   action.sa_handler != SIG_DFL)
                    _exit(2);
            }
            if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
                _exit(3);
            int pipes[2];
            if(pipe(pipes) != 0)
                _exit(4);
            close(pipes[0]);
            {
                test::VmTestContext context;
                Value result = run(
                    context, L"io.f = io.FileIO(" + std::to_wstring(pipes[1]) +
                                 L", 'w', closefd=False)\n"
                                 L"try:\n    io.f.write(b'x')\n    assert "
                                 L"False\nexcept BrokenPipeError as e:\n    "
                                 L"assert e.errno == 32\nio.f.close()\nTrue\n");
                if(result != Value::True())
                    _exit(5);
            }
            struct sigaction action;
            if(sigaction(SIGPIPE, nullptr, &action) != 0 ||
               action.sa_handler != SIG_IGN)
                _exit(6);
            close(pipes[1]);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}
