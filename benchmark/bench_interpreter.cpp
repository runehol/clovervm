#include <benchmark/benchmark.h>

#include "code_object.h"
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

    int64_t fibonacci_value(int64_t n)
    {
        if(n <= 1)
        {
            return n;
        }
        return fibonacci_value(n - 1) + fibonacci_value(n - 2);
    }

    int64_t fibonacci_call_count(int64_t n)
    {
        if(n <= 1)
        {
            return 1;
        }

        int64_t prev2 = 1;
        int64_t prev1 = 1;
        for(int64_t i = 2; i <= n; ++i)
        {
            int64_t current = 1 + prev1 + prev2;
            prev2 = prev1;
            prev1 = current;
        }
        return prev1;
    }

    int64_t triangular_number(int64_t n) { return n * (n - 1) / 2; }

    void set_cpython_comparison_counter(benchmark::State &state,
                                        const char *relative_path, int64_t n,
                                        int64_t expected,
                                        int64_t items_per_iteration);

    template <typename Program>
    std::unique_ptr<Program> make_program(const char *relative_path, int64_t n)
    {
        return std::make_unique<Program>(relative_path, n);
    }

    template <typename Program>
    void run_benchmark_case(benchmark::State &state, const char *relative_path,
                            int64_t n, int64_t expected,
                            int64_t items_per_iteration)
    {
        std::unique_ptr<Program> program;
        try
        {
            program = make_program<Program>(relative_path, n);
        }
        catch(const std::exception &err)
        {
            state.SkipWithError(err.what());
            return;
        }

        if(program->run() != expected)
        {
            throw std::runtime_error("benchmark produced unexpected result");
        }

        for(auto _: state)
        {
            benchmark::DoNotOptimize(program->run());
        }

        state.SetItemsProcessed(state.iterations() * items_per_iteration);

        if constexpr(std::is_same_v<Program, CloverProgram>)
        {
            set_cpython_comparison_counter(state, relative_path, n, expected,
                                           items_per_iteration);
        }
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
        if(program.run() != expected)
        {
            throw std::runtime_error("benchmark produced unexpected result");
        }

        const int64_t batch_size =
            std::max<int64_t>(1, 1000000 / items_per_iteration);
        int64_t total_items = 0;
        double total_seconds = 0.0;
        while(total_seconds < 0.1)
        {
            auto [result, elapsed_seconds] = program.run_batch(batch_size);
            if(result != expected)
            {
                throw std::runtime_error(
                    "benchmark produced unexpected result");
            }
            total_items += batch_size * items_per_iteration;
            total_seconds += elapsed_seconds;
        }

        double throughput = total_items / total_seconds;
        cache.emplace(key, throughput);
        return throughput;
    }

    double measure_clover_items_per_second(const char *relative_path, int64_t n,
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

        CloverProgram program(relative_path, n);
        if(program.run() != expected)
        {
            throw std::runtime_error("benchmark produced unexpected result");
        }

        int64_t total_items = 0;
        auto start = std::chrono::steady_clock::now();
        auto now = start;
        while(std::chrono::duration<double>(now - start).count() < 0.1)
        {
            if(program.run() != expected)
            {
                throw std::runtime_error(
                    "benchmark produced unexpected result");
            }
            total_items += items_per_iteration;
            now = std::chrono::steady_clock::now();
        }

        double total_seconds =
            std::chrono::duration<double>(now - start).count();
        double throughput = total_items / total_seconds;
        cache.emplace(key, throughput);
        return throughput;
    }

    void set_cpython_comparison_counter(benchmark::State &state,
                                        const char *relative_path, int64_t n,
                                        int64_t expected,
                                        int64_t items_per_iteration)
    {
        state.counters["vs_cpython"] =
            measure_clover_items_per_second(relative_path, n, expected,
                                            items_per_iteration) /
            measure_python_items_per_second(relative_path, n, expected,
                                            items_per_iteration);
    }

    int64_t sum_of_products(int64_t n, int64_t inner_iterations)
    {
        int64_t total = 0;
        for(int64_t x = 0; x < n; ++x)
        {
            for(int64_t y = 0; y < inner_iterations; ++y)
            {
                total += x * y;
            }
        }
        return total;
    }

    int64_t class_method_loop_value(int64_t n)
    {
        static constexpr int64_t kScale = 3;
        static constexpr int64_t kBias = 5;

        int64_t total = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            int64_t seed = i;
            int64_t instance_total = i;

            int64_t local0 = i + seed;
            instance_total += local0 * kScale + kBias;
            total += instance_total;

            int64_t local1 = (i + 1) + seed;
            instance_total += local1 * kScale + kBias;
            total += instance_total;

            total += kBias;
        }

        return total;
    }
}  // namespace

template <typename Program>
static void BM_RecursiveFibonacci(benchmark::State &state)
{
    const int64_t n = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/interpreter_recursive_fib.py",
                                n, fibonacci_value(n), fibonacci_call_count(n));
}
BENCHMARK_TEMPLATE(BM_RecursiveFibonacci, CloverProgram)
    ->Name("BM_RecursiveFibonacci")
    ->Arg(20)
    ->Arg(25);

template <typename Program> static void BM_WhileLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/interpreter_while_loop.py",
                                iterations, triangular_number(iterations),
                                iterations);
}
BENCHMARK_TEMPLATE(BM_WhileLoop, CloverProgram)
    ->Name("BM_WhileLoop")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program> static void BM_ForLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/interpreter_for_loop.py",
                                iterations, triangular_number(iterations),
                                iterations);
}
BENCHMARK_TEMPLATE(BM_ForLoop, CloverProgram)
    ->Name("BM_ForLoop")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program>
static void BM_ForLoopSlowPath(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(
        state, "benchmark/interpreter_for_loop_slow_path.py", iterations,
        triangular_number(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_ForLoopSlowPath, CloverProgram)
    ->Name("BM_ForLoopSlowPath")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program>
static void BM_NestedForLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    static constexpr int64_t kInnerIterations = 10;
    run_benchmark_case<Program>(
        state, "benchmark/interpreter_nested_for_loop.py", iterations,
        sum_of_products(iterations, kInnerIterations),
        iterations * kInnerIterations);
}
BENCHMARK_TEMPLATE(BM_NestedForLoop, CloverProgram)
    ->Name("BM_NestedForLoop")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program>
static void BM_ClassMethodLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(
        state, "benchmark/interpreter_class_method_loop.py", iterations,
        class_method_loop_value(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_ClassMethodLoop, CloverProgram)
    ->Name("BM_ClassMethodLoop")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

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
