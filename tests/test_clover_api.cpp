#include <clovervm/clovervm.h>

#include "build_config.h"
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <signal.h>
#include <string>
#include <type_traits>
#include <unistd.h>

namespace
{
    void host_sigpipe_handler(int) {}

    bool has_sigpipe_handler(void (*handler)(int))
    {
        struct sigaction action;
        return sigaction(SIGPIPE, nullptr, &action) == 0 &&
               action.sa_handler == handler;
    }

    void check_preservation(void (*handler)(int))
    {
        if(signal(SIGPIPE, handler) == SIG_ERR)
            _exit(1);
        clover_vm_config config;
        clover_vm_config_init(&config);
        clover_vm *vm = clover_vm_new(&config);
        if(vm == nullptr || !has_sigpipe_handler(handler))
            _exit(2);
        // Caller configuration is not retained by the VM.
        config.signal_policy = CLOVER_SIGNALS_INSTALL;
        if(clover_vm_run_string(vm, "assert 1 + 2 == 3", 0) != CLOVER_STATUS_OK)
            _exit(3);
        clover_vm_destroy(vm);
        if(!has_sigpipe_handler(handler))
            _exit(4);
        _exit(0);
    }
}  // namespace

TEST(CloverApi, ConfigurationDefaultsLeaveSignalsUnchanged)
{
    clover_vm_config config;
    clover_vm_config_init(&config);
    EXPECT_EQ(CLOVER_SIGNALS_LEAVE_UNCHANGED, config.signal_policy);
    config.signal_policy = CLOVER_SIGNALS_INSTALL;
    clover_vm_config_init(&config);
    EXPECT_EQ(CLOVER_SIGNALS_LEAVE_UNCHANGED, config.signal_policy);
}

TEST(CloverApi, DefaultConfigurationPreservesDefaultSignalDisposition)
{
    ASSERT_EXIT(check_preservation(SIG_DFL), ::testing::ExitedWithCode(0), "");
}

TEST(CloverApi, DefaultConfigurationPreservesCustomHostHandler)
{
    ASSERT_EXIT(check_preservation(host_sigpipe_handler),
                ::testing::ExitedWithCode(0), "");
}

TEST(CloverApi, DefaultConfigurationPreservesIgnoredSignal)
{
    ASSERT_EXIT(check_preservation(SIG_IGN), ::testing::ExitedWithCode(0), "");
}

TEST(CloverApi, InstallationIsProcessWideAndNotRestoredByDestruction)
{
    ASSERT_EXIT(
        {
            if(signal(SIGPIPE, host_sigpipe_handler) == SIG_ERR)
                _exit(1);
            clover_vm *installed;
            {
                clover_vm_config config;
                clover_vm_config_init(&config);
                config.signal_policy = CLOVER_SIGNALS_INSTALL;
                installed = clover_vm_new(&config);
            }
            if(installed == nullptr || !has_sigpipe_handler(SIG_IGN))
                _exit(2);
            clover_vm_config leave;
            clover_vm_config_init(&leave);
            clover_vm *unchanged = clover_vm_new(&leave);
            if(unchanged == nullptr || !has_sigpipe_handler(SIG_IGN))
                _exit(3);
            clover_vm_destroy(installed);
            if(!has_sigpipe_handler(SIG_IGN))
                _exit(4);
            if(clover_vm_run_string(unchanged, "assert 1 + 2 == 3", 0) !=
               CLOVER_STATUS_OK)
                _exit(5);
            clover_vm_destroy(unchanged);
            if(!has_sigpipe_handler(SIG_IGN))
                _exit(6);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(CloverApi, InvalidConfigurationDoesNotChangeSignalDisposition)
{
    ASSERT_EXIT(
        {
            if(signal(SIGPIPE, host_sigpipe_handler) == SIG_ERR)
                _exit(1);
            if(clover_vm_new(nullptr) != nullptr ||
               !has_sigpipe_handler(host_sigpipe_handler))
                _exit(2);
            clover_vm_config config;
            clover_vm_config_init(&config);
            // Model invalid C input without an out-of-range C++ enum
            // conversion.
            std::underlying_type_t<clover_signal_policy> invalid = 99;
            std::memcpy(&config.signal_policy, &invalid, sizeof(invalid));
            if(clover_vm_new(&config) != nullptr ||
               !has_sigpipe_handler(host_sigpipe_handler))
                _exit(3);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(CloverApi, CliInstallsSignalPolicyBeforeRunningPython)
{
    ASSERT_EXIT(
        {
            if(signal(SIGPIPE, SIG_DFL) == SIG_ERR)
                _exit(1);
            int pipes[2];
            if(pipe(pipes) != 0)
                _exit(2);
            close(pipes[0]);
            std::string source =
                "import io\nwith io.FileIO(" + std::to_string(pipes[1]) +
                ", 'w', closefd=False) as stream:\n"
                "    try:\n        stream.write(b'x')\n        assert False\n"
                "    except BrokenPipeError:\n        pass\n";
            std::string executable =
                (std::filesystem::path(CL_BUILD_STDLIB_DIR).parent_path() /
                 "src" / "clovervm")
                    .string();
            execl(executable.c_str(), executable.c_str(), "-c", source.c_str(),
                  static_cast<char *>(nullptr));
            _exit(127);
        },
        ::testing::ExitedWithCode(0), "");
}
