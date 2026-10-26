#include "builtin_types/file_io.h"
#include "builtin_types/tuple.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/class_object.h"
#include "platform/file_io.h"
#include "test_helpers.h"
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <string>
#ifdef __APPLE__
#include <sys/mman.h>
#endif
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

TEST(FileIO, SharedLayoutHasFivePythonSlotsAndNoCleanupHook)
{
    EXPECT_EQ(sizeof(RawIOBaseObject), CL_OFFSETOF(FileIOObject, name));
    EXPECT_EQ(sizeof(InlineSlotObject) + 5 * sizeof(Value),
              CL_OFFSETOF(FileIOObject, fd));
    const auto &release = release_descriptor_for(NativeLayoutId::FileIO);
    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(InlineSlotObject::native_static_release_count() + 5,
              release.static_release_count);
    EXPECT_EQ(nullptr, release.custom_dealloc);
    EXPECT_EQ(sizeof(FileIOObject),
              object_size_descriptor_for(NativeLayoutId::FileIO)
                  .static_size_in_bytes);
    EXPECT_TRUE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::FileIO, 5));
    EXPECT_FALSE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::FileIO, 6));
}

TEST(IOBase, NativePrefixMetadataAndMostDerivedStorage)
{
    EXPECT_TRUE((std::is_base_of_v<IOBaseObject, RawIOBaseObject>));
    EXPECT_TRUE((std::is_base_of_v<IOBaseObject, BufferedIOBaseObject>));
    EXPECT_TRUE((std::is_base_of_v<RawIOBaseObject, FileIOObject>));
    for(auto layout: {NativeLayoutId::IOBase, NativeLayoutId::RawIOBase,
                      NativeLayoutId::BufferedIOBase})
    {
        const auto &release = release_descriptor_for(layout);
        EXPECT_TRUE(native_layout_has_inline_slots(layout));
        EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
        EXPECT_EQ(InlineSlotObject::native_static_release_count() + 1,
                  release.static_release_count);
        EXPECT_EQ(nullptr, release.custom_dealloc);
        EXPECT_TRUE(native_inline_slot_capacity_is_valid(layout, 1));
        EXPECT_FALSE(native_inline_slot_capacity_is_valid(layout, 2));
    }
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *file =
        context.thread()->class_for_native_layout(NativeLayoutId::FileIO);
    auto selected = select_instance_storage(
        TValue<Tuple>::from_value_assumed(file->get_mro_value()), 10);
    ASSERT_FALSE(selected.has_exception());
    EXPECT_EQ(NativeLayoutId::RawIOBase, selected.value().layout);
    EXPECT_EQ(1u, selected.value().inline_slot_capacity);
    EXPECT_EQ(
        context.thread()->class_for_native_layout(NativeLayoutId::RawIOBase),
        selected.value().builtin_provider);
}

TEST(FileIO, SubclassDescriptorOwnershipAndAbandonment)
{
    TestFd owned(temporary_fd());
    TestFd borrowed(temporary_fd());
    TestFd abandoned(temporary_fd());
    ASSERT_GE(owned.fd, 0);
    ASSERT_GE(borrowed.fd, 0);
    ASSERT_GE(abandoned.fd, 0);
    {
        test::VmTestContext context;
        EXPECT_EQ(
            Value::None(),
            run(context,
                L"class Derived(io.FileIO):\n    pass\nio.Derived = Derived\n"
                L"io.f = Derived(" +
                    std::to_wstring(owned.fd) +
                    L")\n"
                    L"io.f.close()\nNone\n"));
        int number = owned.release();
        EXPECT_EQ(-1, fcntl(number, F_GETFD));
        EXPECT_EQ(EBADF, errno);
        EXPECT_EQ(Value::None(),
                  run(context, L"io.f = io.Derived(" +
                                   std::to_wstring(borrowed.fd) +
                                   L", closefd=False)\nio.f.close()\nNone\n"));
        EXPECT_GE(fcntl(borrowed.fd, F_GETFD), 0);
        EXPECT_EQ(Value::None(),
                  run(context, L"io.f = io.Derived(" +
                                   std::to_wstring(abandoned.fd) +
                                   L")\nio.f = None\n"));
        context.vm().run_heap_reclamation();
        EXPECT_GE(fcntl(abandoned.fd, F_GETFD), 0);
    }
    EXPECT_GE(fcntl(abandoned.fd, F_GETFD), 0);
}

TEST(FileIO, DirectBaseClosePreservesDescriptorButFlushUsesBaseState)
{
    TestFd fd(temporary_fd());
    ASSERT_GE(fd.fd, 0);
    test::VmTestContext context;
    EXPECT_EQ(
        Value::True(),
        run(context, L"io.f = io.FileIO(" + std::to_wstring(fd.fd) +
                         L", closefd=False)\n"
                         L"assert io.f.flush() is None\n"
                         L"io.IOBase.close(io.f)\nassert not io.f.closed\n"
                         L"assert io.f._checkClosed() is None\n"
                         L"assert io.f.read(1) == b''\n"
                         L"assert io.f.readinto(bytearray(1)) == 0\n"
                         L"try:\n    io.f.flush()\n    assert False\nexcept "
                         L"ValueError:\n    pass\nTrue\n"));
    EXPECT_GE(fcntl(fd.fd, F_GETFD), 0);
    EXPECT_EQ(Value::None(),
              run(context, L"io.f.close()\nassert io.f.closed\nNone\n"));
    EXPECT_GE(fcntl(fd.fd, F_GETFD), 0);
}

TEST(FileIO, FailedFlushStillClosesAndCloseErrorWins)
{
    for(const wchar_t *flush_error: {L"ZeroDivisionError", L"StopIteration"})
    {
        for(bool close_error: {false, true})
        {
            TestFd fd(temporary_fd());
            ASSERT_GE(fd.fd, 0);
            test::VmTestContext context;
            ASSERT_EQ(
                Value::None(),
                run(context, L"io.f = io.FileIO(" + std::to_wstring(fd.fd) +
                                 L")\n"
                                 L"io.calls = 0\n"
                                 L"def fail():\n    io.calls += 1\n    " +
                                 (std::wstring(flush_error) == L"StopIteration"
                                      ? L"next(iter(()))\n"
                                      : L"raise ZeroDivisionError('flush')\n") +
                                 L"io.f.flush = fail\nNone\n"));
            int number = fd.release();
            if(close_error)
                ASSERT_EQ(0, close(number));
            std::wstring check_errno =
                close_error ? L"    assert error.errno == " +
                                  std::to_wstring(EBADF) + L"\n"
                : std::wstring(flush_error) == L"StopIteration"
                    ? L"    assert error.args == ()\n"
                    : L"    assert str(error) == 'flush'\n";
            EXPECT_EQ(
                Value::True(),
                run(context,
                    std::wstring(
                        L"try:\n    io.f.close()\n    assert False\nexcept ") +
                        (close_error ? L"OSError" : flush_error) +
                        L" as error:\n" + check_errno +
                        L"assert io.f.closed\n"
                        L"try:\n    io.IOBase.flush(io.f)\n    assert "
                        L"False\nexcept ValueError:\n    pass\n"
                        L"assert io.f.close() is None\nassert io.calls == "
                        L"1\nTrue\n"));
            EXPECT_EQ(-1, fcntl(number, F_GETFD));
            EXPECT_EQ(EBADF, errno);
        }
    }
}

TEST(FileIO, FailedFlushDetachesBorrowedDescriptorWithoutClosingIt)
{
    for(const wchar_t *flush_error: {L"ZeroDivisionError", L"StopIteration"})
    {
        TestFd fd(temporary_fd());
        ASSERT_GE(fd.fd, 0);
        test::VmTestContext context;
        EXPECT_EQ(Value::True(),
                  run(context,
                      L"io.f = io.FileIO(" + std::to_wstring(fd.fd) +
                          L", closefd=False)\n"
                          L"io.calls = 0\n"
                          L"def fail():\n    io.calls += 1\n    " +
                          (std::wstring(flush_error) == L"StopIteration"
                               ? L"next(iter(()))\n"
                               : L"raise ZeroDivisionError('flush')\n") +
                          L"io.f.flush = fail\n"
                          L"try:\n    io.f.close()\n    assert False\nexcept " +
                          std::wstring(flush_error) +
                          L":\n    pass\n"
                          L"assert io.f.closed\n"
                          L"try:\n    io.IOBase.flush(io.f)\n    assert "
                          L"False\nexcept ValueError:\n    pass\n"
                          L"assert io.f.close() is None\nassert io.calls == "
                          L"1\nTrue\n"));
        EXPECT_GE(fcntl(fd.fd, F_GETFD), 0);
        char byte;
        EXPECT_EQ(0, read(fd.fd, &byte, 1));
    }
}

TEST(FileIO, ReentrantFlushCloseDoesNotCloseReusedDescriptor)
{
    TestFd fd(temporary_fd());
    TestFd replacement(temporary_fd());
    ASSERT_GE(fd.fd, 0);
    ASSERT_GE(replacement.fd, 0);
    test::VmTestContext context;
    EXPECT_EQ(Value::None(),
              run(context, L"io.f = io.FileIO(" + std::to_wstring(fd.fd) +
                               L")\n"
                               L"def noop():\n    pass\n"
                               L"def flush():\n    io.f.flush = noop\n "
                               L"   io.f.close()\n"
                               L"io.f.flush = flush\nio.f.close()\nNone\n"));
    int number = fd.release();
    EXPECT_EQ(-1, fcntl(number, F_GETFD));
    TestFd reused(dup2(replacement.fd, number));
    ASSERT_EQ(number, reused.fd);
    EXPECT_EQ(Value::None(), run(context, L"io.f.close()\n"));
    EXPECT_GE(fcntl(number, F_GETFD), 0);
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

#ifdef __APPLE__
TEST(FileIO, PlatformReadCapsOversizedMacOSRequest)
{
    TestFd descriptor(open("/dev/null", O_RDONLY));
    ASSERT_GE(descriptor.fd, 0);
    size_t size = static_cast<size_t>(INT_MAX) + 1;
    // Reserve valid address space without touching its pages. /dev/null writes
    // no data, so this checks the OS count limit without physical allocation.
    void *buffer = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    ASSERT_NE(MAP_FAILED, buffer);
    FileResult result = platform_file_read(descriptor.fd, buffer, size);
    EXPECT_EQ(0, result.error);
    EXPECT_EQ(0, result.value);
    EXPECT_EQ(0, munmap(buffer, size));
}
#endif

TEST(FileIO, ReadIntoPreservesShortResultsAndBorrowedStorage)
{
    int descriptors[2];
    ASSERT_EQ(0, pipe(descriptors));
    TestFd reader(descriptors[0]);
    TestFd writer(descriptors[1]);
    ASSERT_EQ(0, fcntl(reader.fd, F_SETFL, O_NONBLOCK));
    test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    ASSERT_EQ(Value::None(),
              run(context, L"io.reader = io.FileIO(" +
                               std::to_wstring(reader.fd) +
                               L", closefd=False)\n"
                               L"io.buffer = bytearray(b'XXXXX')\n"
                               L"assert io.reader.readinto(io.buffer) is None\n"
                               L"assert io.buffer == b'XXXXX'\nNone\n"));
    ASSERT_EQ(2, write(writer.fd, "AB", 2));
    EXPECT_EQ(Value::None(),
              run(context,
                  L"assert io.reader.readinto(bytearray()) == 0\n"
                  L"assert io.reader.readinto(io.buffer) == 2\n"
                  L"assert len(io.buffer) == 5 and io.buffer == b'ABXXX'\n"
                  L"assert io.reader.readinto(io.buffer) is None\n"
                  L"assert io.buffer == b'ABXXX'\nNone\n"));
    context.vm().run_heap_reclamation();
    EXPECT_EQ(Value::None(), run(context, L"io.buffer.extend(b'YZ')\nassert "
                                          L"io.buffer == b'ABXXXYZ'\nNone\n"));
    ASSERT_EQ(0, close(writer.release()));
    EXPECT_EQ(
        Value::None(),
        run(context,
            L"assert io.reader.readinto(io.buffer) == 0\n"
            L"assert io.buffer == b'ABXXXYZ'\nio.reader.close()\nNone\n"));
    EXPECT_GE(fcntl(reader.fd, F_GETFD), 0);
}

TEST(FileIO, ReadIntoReportsExternalCloseEvenForEmptyDestination)
{
    TestFd descriptor(temporary_fd());
    ASSERT_GE(descriptor.fd, 0);
    test::VmTestContext context;
    ASSERT_EQ(Value::None(), run(context, L"io.reader = io.FileIO(" +
                                              std::to_wstring(descriptor.fd) +
                                              L", closefd=False)\nNone\n"));
    ASSERT_EQ(0, close(descriptor.release()));
    EXPECT_EQ(
        Value::None(),
        run(context,
            L"for buffer in (bytearray(), bytearray(b'XXXXX')):\n"
            L"    try:\n        io.reader.readinto(buffer)\n        assert "
            L"False\n"
            L"    except OSError as error:\n        assert error.errno == " +
                std::to_wstring(EBADF) +
                L"\n"
                L"    assert buffer == b'' or buffer == b'XXXXX'\n"
                L"assert not io.reader.closed\nio.reader.close()\nNone\n"));
}

TEST(FileIO, ReadAllReturnsAvailablePipeDataOrNoneBeforeEOF)
{
    int descriptors[2];
    ASSERT_EQ(0, pipe(descriptors));
    TestFd reader(descriptors[0]);
    TestFd writer(descriptors[1]);
    ASSERT_EQ(0, fcntl(reader.fd, F_SETFL, O_NONBLOCK));
    test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    ASSERT_EQ(Value::None(),
              run(context, L"io.reader = io.FileIO(" +
                               std::to_wstring(reader.fd) +
                               L", closefd=False)\n"
                               L"assert io.reader.readall() is None\nNone\n"));
    ASSERT_EQ(3, write(writer.fd, "ABC", 3));
    EXPECT_EQ(Value::None(),
              run(context, L"assert io.reader.readall() == b'ABC'\n"
                           L"assert io.reader.readall() is None\nNone\n"));
    ASSERT_EQ(2, write(writer.fd, "DE", 2));
    EXPECT_EQ(Value::None(),
              run(context,
                  L"assert io.RawIOBase.readall(io.reader) == b'DE'\n"
                  L"assert io.RawIOBase.readall(io.reader) is None\nNone\n"));
    ASSERT_EQ(0, close(writer.release()));
    EXPECT_EQ(Value::None(),
              run(context, L"assert io.reader.readall() == b''\n"
                           L"assert io.RawIOBase.readall(io.reader) == "
                           L"b''\nio.reader.close()\nNone\n"));
    EXPECT_GE(fcntl(reader.fd, F_GETFD), 0);
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
