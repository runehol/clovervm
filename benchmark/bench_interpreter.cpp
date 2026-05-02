#include <benchmark/benchmark.h>

#include "code_object.h"
#include "cpp_benchmarks.h"
#include "parser.h"
#include "thread_state.h"
#include "value.h"
#include "virtual_machine.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>

namespace
{
    using namespace cl;

    struct BenchmarkCase
    {
        int64_t (*run)(int64_t);
        int64_t (*items)(int64_t);
    };

    BenchmarkCase get_benchmark_case(const char *relative_path)
    {
        static const std::unordered_map<std::string, BenchmarkCase> cases = {
            {"benchmark/recursive_fib.py",
             {benchmark_cpp::recursive_fib_run,
              benchmark_cpp::recursive_fib_items}},
            {"benchmark/while_loop.py",
             {benchmark_cpp::while_loop_run, benchmark_cpp::while_loop_items}},
            {"benchmark/for_loop.py",
             {benchmark_cpp::for_loop_run, benchmark_cpp::for_loop_items}},
            {"benchmark/for_loop_slow_path.py",
             {benchmark_cpp::for_loop_slow_path_run,
              benchmark_cpp::for_loop_slow_path_items}},
            {"benchmark/nested_for_loop.py",
             {benchmark_cpp::nested_for_loop_run,
              benchmark_cpp::nested_for_loop_items}},
            {"benchmark/class_instantiation.py",
             {benchmark_cpp::class_instantiation_run,
              benchmark_cpp::class_instantiation_items}},
            {"benchmark/class_instantiation_with_init.py",
             {benchmark_cpp::class_instantiation_with_init_run,
              benchmark_cpp::class_instantiation_with_init_items}},
            {"benchmark/instance_attribute_add_member.py",
             {benchmark_cpp::instance_attribute_add_member_run,
              benchmark_cpp::instance_attribute_add_member_items}},
            {"benchmark/instance_attribute_add_after_construction.py",
             {benchmark_cpp::instance_attribute_add_after_construction_run,
              benchmark_cpp::instance_attribute_add_after_construction_items}},
            {"benchmark/instance_attribute_read.py",
             {benchmark_cpp::instance_attribute_read_run,
              benchmark_cpp::instance_attribute_read_items}},
            {"benchmark/class_attribute_read.py",
             {benchmark_cpp::class_attribute_read_run,
              benchmark_cpp::class_attribute_read_items}},
            {"benchmark/instance_attribute_write.py",
             {benchmark_cpp::instance_attribute_write_run,
              benchmark_cpp::instance_attribute_write_items}},
            {"benchmark/class_attribute_write.py",
             {benchmark_cpp::class_attribute_write_run,
              benchmark_cpp::class_attribute_write_items}},
            {"benchmark/method_call.py",
             {benchmark_cpp::method_call_run,
              benchmark_cpp::method_call_items}},
            {"benchmark/function_default_parameter.py",
             {benchmark_cpp::function_default_parameter_run,
              benchmark_cpp::function_default_parameter_items}},
            {"benchmark/function_varargs.py",
             {benchmark_cpp::function_varargs_run,
              benchmark_cpp::function_varargs_items}},
            {"benchmark/function_varargs_with_positional.py",
             {benchmark_cpp::function_varargs_with_positional_run,
              benchmark_cpp::function_varargs_with_positional_items}},
            {"benchmark/function_default_varargs.py",
             {benchmark_cpp::function_default_varargs_run,
              benchmark_cpp::function_default_varargs_items}},
            {"benchmark/method_call_class_attribute_write.py",
             {benchmark_cpp::method_call_class_attribute_write_run,
              benchmark_cpp::method_call_class_attribute_write_items}},
            {"benchmark/pystone_lite.py",
             {benchmark_cpp::pystone_lite_run,
              benchmark_cpp::pystone_lite_items}},
            {"benchmark/pystone_arithmetic.py",
             {benchmark_cpp::pystone_arithmetic_run,
              benchmark_cpp::pystone_arithmetic_items}},
        };

        auto it = cases.find(relative_path);
        if(it == cases.end())
        {
            throw std::runtime_error("no C++ benchmark equivalent registered");
        }
        return it->second;
    }

    std::string load_program_source(const char *relative_path)
    {
        std::ifstream input(std::string(CLOVERVM_SOURCE_DIR) + "/" +
                            relative_path);
        if(!input)
        {
            throw std::runtime_error("failed to open benchmark source file");
        }

        return std::string((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    }

    std::wstring make_clover_source(const char *relative_path, int64_t n)
    {
        std::string source = load_program_source(relative_path);
        source += "\nrun(" + std::to_string(n) + ")\n";
        return std::wstring(source.begin(), source.end());
    }

    class CloverProgram
    {
    public:
        CloverProgram(const char *relative_path, int64_t n)
            : thread_(vm_.get_default_thread()),
              code_(
                  thread_->compile(make_clover_source(relative_path, n).c_str(),
                                   StartRule::File))
        {
        }

        int64_t run()
        {
            Value actual = thread_->run(code_);
            if(!actual.is_smi())
            {
                throw std::runtime_error(
                    "benchmark produced non-integer Clover result");
            }
            return actual.get_smi();
        }

    private:
        VirtualMachine vm_;
        ThreadState *thread_;
        CodeObject *code_;
    };

    class CppProgram
    {
    public:
        CppProgram(const char *relative_path, int64_t n)
            : fn_(get_benchmark_case(relative_path).run), n_(n)
        {
        }

        int64_t run() { return fn_(n_); }

    private:
        int64_t (*fn_)(int64_t);
        int64_t n_;
    };

    class PythonSubprocess
    {
    public:
        PythonSubprocess(const char *relative_path, int64_t n)
        {
            int stdin_pipe[2];
            int stdout_pipe[2];
            if(pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0)
            {
                throw std::runtime_error("failed to create pipes for python");
            }

            pid_ = fork();
            if(pid_ < 0)
            {
                throw std::runtime_error("failed to fork python subprocess");
            }
            if(pid_ == 0)
            {
                dup2(stdin_pipe[0], STDIN_FILENO);
                dup2(stdout_pipe[1], STDOUT_FILENO);
                close(stdin_pipe[0]);
                close(stdin_pipe[1]);
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);

                std::string script_path = std::string(CLOVERVM_SOURCE_DIR) +
                                          "/benchmark/cpython_runner.py";
                std::string benchmark_path =
                    std::string(CLOVERVM_SOURCE_DIR) + "/" + relative_path;
                std::string n_arg = std::to_string(n);
                execlp("python3", "python3", script_path.c_str(),
                       benchmark_path.c_str(), n_arg.c_str(),
                       static_cast<char *>(nullptr));
                _exit(127);
            }

            close(stdin_pipe[0]);
            close(stdout_pipe[1]);
            stdin_ = fdopen(stdin_pipe[1], "w");
            stdout_ = fdopen(stdout_pipe[0], "r");
            if(stdin_ == nullptr || stdout_ == nullptr)
            {
                throw std::runtime_error("failed to open python stdio streams");
            }

            char buffer[256];
            if(fgets(buffer, sizeof(buffer), stdout_) == nullptr)
            {
                throw std::runtime_error("python subprocess failed to start");
            }
            if(std::string(buffer) != "READY\n")
            {
                throw std::runtime_error(
                    "python subprocess reported bad ready state");
            }
        }

        ~PythonSubprocess()
        {
            if(stdin_ != nullptr)
            {
                fputs("QUIT\n", stdin_);
                fflush(stdin_);
                fclose(stdin_);
            }
            if(stdout_ != nullptr)
            {
                fclose(stdout_);
            }
            if(pid_ > 0)
            {
                int status = 0;
                (void)waitpid(pid_, &status, 0);
            }
        }

        int64_t run() { return run_batch(1).first; }

        std::pair<int64_t, double> run_batch(int64_t batch_size)
        {
            std::string command = "RUN " + std::to_string(batch_size) + "\n";
            if(fputs(command.c_str(), stdin_) == EOF || fflush(stdin_) != 0)
            {
                throw std::runtime_error(
                    "failed to send run command to python");
            }

            char buffer[256];
            if(fgets(buffer, sizeof(buffer), stdout_) == nullptr)
            {
                throw std::runtime_error(
                    "failed to read python benchmark result");
            }

            std::istringstream input(buffer);
            int64_t result = 0;
            double elapsed_seconds = 0.0;
            if(!(input >> result >> elapsed_seconds))
            {
                throw std::runtime_error(
                    "failed to parse python benchmark result");
            }

            return {result, elapsed_seconds};
        }

    private:
        pid_t pid_ = -1;
        FILE *stdin_ = nullptr;
        FILE *stdout_ = nullptr;
    };

    std::string query_python_version()
    {
        int stdout_pipe[2];
        if(pipe(stdout_pipe) != 0)
        {
            throw std::runtime_error(
                "failed to create pipe for python version");
        }

        pid_t pid = fork();
        if(pid < 0)
        {
            throw std::runtime_error("failed to fork for python version");
        }
        if(pid == 0)
        {
            dup2(stdout_pipe[1], STDOUT_FILENO);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            execlp("python3", "python3", "-c",
                   "import platform, sys; "
                   "print(f\"{platform.python_implementation()} "
                   "{sys.version.split()[0]}\")",
                   static_cast<char *>(nullptr));
            _exit(127);
        }

        close(stdout_pipe[1]);
        FILE *output = fdopen(stdout_pipe[0], "r");
        if(output == nullptr)
        {
            throw std::runtime_error("failed to open python version stream");
        }

        char buffer[256];
        if(fgets(buffer, sizeof(buffer), output) == nullptr)
        {
            fclose(output);
            int status = 0;
            (void)waitpid(pid, &status, 0);
            throw std::runtime_error("failed to read python version");
        }

        fclose(output);
        int status = 0;
        (void)waitpid(pid, &status, 0);

        std::string version(buffer);
        if(!version.empty() && version.back() == '\n')
        {
            version.pop_back();
        }
        return version;
    }

    template <typename Program>
    std::unique_ptr<Program> make_program(const char *relative_path, int64_t n)
    {
        return std::make_unique<Program>(relative_path, n);
    }

    void verify_benchmark_result(const char *relative_path, int64_t n,
                                 int64_t expected, int64_t actual)
    {
        if(actual == expected)
        {
            return;
        }

        throw std::runtime_error(std::string("benchmark produced unexpected "
                                             "result for ") +
                                 relative_path + "/" + std::to_string(n) +
                                 ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }

    double measure_python_items_per_second(const char *relative_path, int64_t n,
                                           int64_t expected,
                                           int64_t items_per_iteration)
    {
        static std::unordered_map<std::string, double> cache;

        std::string key = std::string(relative_path) + "#" + std::to_string(n);
        auto it = cache.find(key);
        if(it != cache.end())
        {
            return it->second;
        }

        PythonSubprocess program(relative_path, n);
        verify_benchmark_result(relative_path, n, expected, program.run());

        const int64_t batch_size =
            std::max<int64_t>(1, 1000000 / items_per_iteration);
        int64_t total_items = 0;
        double total_seconds = 0.0;
        while(total_seconds < 0.1)
        {
            auto [result, elapsed_seconds] = program.run_batch(batch_size);
            verify_benchmark_result(relative_path, n, expected, result);
            total_items += batch_size * items_per_iteration;
            total_seconds += elapsed_seconds;
        }

        double throughput = total_items / total_seconds;
        cache.emplace(key, throughput);
        return throughput;
    }

    template <typename Program>
    double measure_items_per_second(const char *relative_path, int64_t n,
                                    int64_t expected,
                                    int64_t items_per_iteration)
    {
        static std::unordered_map<std::string, double> cache;

        std::string key = std::string(relative_path) + "#" + std::to_string(n);
        auto it = cache.find(key);
        if(it != cache.end())
        {
            return it->second;
        }

        Program program(relative_path, n);
        verify_benchmark_result(relative_path, n, expected, program.run());

        int64_t total_items = 0;
        auto start = std::chrono::steady_clock::now();
        auto now = start;
        while(std::chrono::duration<double>(now - start).count() < 0.1)
        {
            verify_benchmark_result(relative_path, n, expected, program.run());
            total_items += items_per_iteration;
            now = std::chrono::steady_clock::now();
        }

        double total_seconds =
            std::chrono::duration<double>(now - start).count();
        double throughput = total_items / total_seconds;
        cache.emplace(key, throughput);
        return throughput;
    }

    void set_comparison_counters(benchmark::State &state,
                                 const char *relative_path, int64_t n,
                                 int64_t expected, int64_t items_per_iteration)
    {
        double clover_items_per_second =
            measure_items_per_second<CloverProgram>(relative_path, n, expected,
                                                    items_per_iteration);
        state.counters["vs_cpython"] =
            clover_items_per_second /
            measure_python_items_per_second(relative_path, n, expected,
                                            items_per_iteration);
    }

    template <typename Program>
    void run_benchmark_case(benchmark::State &state, const char *relative_path,
                            int64_t n)
    {
        try
        {
            BenchmarkCase benchmark_case = get_benchmark_case(relative_path);
            int64_t expected = benchmark_case.run(n);
            int64_t items_per_iteration = benchmark_case.items(n);

            std::unique_ptr<Program> program;
            program = make_program<Program>(relative_path, n);

            verify_benchmark_result(relative_path, n, expected,
                                    program->run());

            for(auto _: state)
            {
                benchmark::DoNotOptimize(program->run());
            }

            state.SetItemsProcessed(state.iterations() * items_per_iteration);

            if constexpr(std::is_same_v<Program, CloverProgram>)
            {
                set_comparison_counters(state, relative_path, n, expected,
                                        items_per_iteration);
            }
        }
        catch(const std::exception &err)
        {
            state.SkipWithError(err.what());
        }
    }
}  // namespace

template <typename Program>
static void BM_RecursiveFibonacci(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/recursive_fib.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_RecursiveFibonacci, CloverProgram)
    ->Name("BM_RecursiveFibonacci")
    ->Arg(25);

template <typename Program> static void BM_WhileLoop(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/while_loop.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_WhileLoop, CloverProgram)
    ->Name("BM_WhileLoop")
    ->Arg(100000);

template <typename Program> static void BM_ForLoop(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/for_loop.py", state.range(0));
}
BENCHMARK_TEMPLATE(BM_ForLoop, CloverProgram)->Name("BM_ForLoop")->Arg(100000);

template <typename Program>
static void BM_ForLoopSlowPath(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/for_loop_slow_path.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_ForLoopSlowPath, CloverProgram)
    ->Name("BM_ForLoopSlowPath")
    ->Arg(100000);

template <typename Program>
static void BM_NestedForLoop(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/nested_for_loop.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_NestedForLoop, CloverProgram)
    ->Name("BM_NestedForLoop")
    ->Arg(100000);

template <typename Program>
static void BM_ClassInstantiationNoInit(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/class_instantiation.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_ClassInstantiationNoInit, CloverProgram)
    ->Name("BM_ClassInstantiationNoInit")
    ->Arg(100000);

template <typename Program>
static void BM_ClassInstantiationWithInit(benchmark::State &state)
{
    run_benchmark_case<Program>(
        state, "benchmark/class_instantiation_with_init.py", state.range(0));
}
BENCHMARK_TEMPLATE(BM_ClassInstantiationWithInit, CloverProgram)
    ->Name("BM_ClassInstantiationWithInit")
    ->Arg(100000);

template <typename Program>
static void BM_InstanceAttributeAddMember(benchmark::State &state)
{
    run_benchmark_case<Program>(
        state, "benchmark/instance_attribute_add_member.py", state.range(0));
}
BENCHMARK_TEMPLATE(BM_InstanceAttributeAddMember, CloverProgram)
    ->Name("BM_InstanceAttributeAddMember")
    ->Arg(100000);

template <typename Program>
static void BM_InstanceAttributeAddAfterConstruction(benchmark::State &state)
{
    run_benchmark_case<Program>(
        state, "benchmark/instance_attribute_add_after_construction.py",
        state.range(0));
}
BENCHMARK_TEMPLATE(BM_InstanceAttributeAddAfterConstruction, CloverProgram)
    ->Name("BM_InstanceAttributeAddAfterConstruction")
    ->Arg(100000);

template <typename Program>
static void BM_InstanceAttributeRead(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/instance_attribute_read.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_InstanceAttributeRead, CloverProgram)
    ->Name("BM_InstanceAttributeRead")
    ->Arg(100000);

template <typename Program>
static void BM_ClassAttributeRead(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/class_attribute_read.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_ClassAttributeRead, CloverProgram)
    ->Name("BM_ClassAttributeRead")
    ->Arg(100000);

template <typename Program>
static void BM_InstanceAttributeWrite(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/instance_attribute_write.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_InstanceAttributeWrite, CloverProgram)
    ->Name("BM_InstanceAttributeWrite")
    ->Arg(100000);

template <typename Program>
static void BM_ClassAttributeWrite(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/class_attribute_write.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_ClassAttributeWrite, CloverProgram)
    ->Name("BM_ClassAttributeWrite")
    ->Arg(100000);

template <typename Program> static void BM_MethodCall(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/method_call.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_MethodCall, CloverProgram)
    ->Name("BM_MethodCall")
    ->Arg(100000);

template <typename Program>
static void BM_FunctionDefaultParameter(benchmark::State &state)
{
    run_benchmark_case<Program>(
        state, "benchmark/function_default_parameter.py", state.range(0));
}
BENCHMARK_TEMPLATE(BM_FunctionDefaultParameter, CloverProgram)
    ->Name("BM_FunctionDefaultParameter")
    ->Arg(100000);

template <typename Program>
static void BM_FunctionVarargs(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/function_varargs.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_FunctionVarargs, CloverProgram)
    ->Name("BM_FunctionVarargs")
    ->Arg(100000);

template <typename Program>
static void BM_FunctionVarargsWithPositional(benchmark::State &state)
{
    run_benchmark_case<Program>(
        state, "benchmark/function_varargs_with_positional.py", state.range(0));
}
BENCHMARK_TEMPLATE(BM_FunctionVarargsWithPositional, CloverProgram)
    ->Name("BM_FunctionVarargsWithPositional")
    ->Arg(100000);

template <typename Program>
static void BM_FunctionDefaultVarargs(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/function_default_varargs.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_FunctionDefaultVarargs, CloverProgram)
    ->Name("BM_FunctionDefaultVarargs")
    ->Arg(100000);

template <typename Program>
static void BM_MethodCallClassAttributeWrite(benchmark::State &state)
{
    run_benchmark_case<Program>(
        state, "benchmark/method_call_class_attribute_write.py",
        state.range(0));
}
BENCHMARK_TEMPLATE(BM_MethodCallClassAttributeWrite, CloverProgram)
    ->Name("BM_MethodCallClassAttributeWrite")
    ->Arg(100000);

template <typename Program> static void BM_PystoneLite(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/pystone_lite.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_PystoneLite, CloverProgram)
    ->Name("BM_PystoneLite")
    ->Arg(10000);

template <typename Program>
static void BM_PystoneArithmetic(benchmark::State &state)
{
    run_benchmark_case<Program>(state, "benchmark/pystone_arithmetic.py",
                                state.range(0));
}
BENCHMARK_TEMPLATE(BM_PystoneArithmetic, CloverProgram)
    ->Name("BM_PystoneArithmetic")
    ->Arg(10000);

int main(int argc, char **argv)
{
    try
    {
        benchmark::AddCustomContext("CPython", query_python_version());
    }
    catch(const std::exception &err)
    {
        benchmark::AddCustomContext("CPython", std::string("unavailable (") +
                                                   err.what() + ")");
    }

    benchmark::Initialize(&argc, argv);
    if(benchmark::ReportUnrecognizedArguments(argc, argv))
    {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
