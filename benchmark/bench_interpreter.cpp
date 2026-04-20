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

    int64_t class_instantiation_value(int64_t n) { return n; }

    int64_t instance_attribute_write_value(int64_t n)
    {
        int64_t total = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            int64_t left = i;
            int64_t right = i + 1;
            total += left + right;
        }
        return total;
    }

    int64_t method_call_value(int64_t n) { return triangular_number(n) + n; }

    struct PystoneLiteRecord
    {
        PystoneLiteRecord *ptr_comp = nullptr;
        int64_t discr = 0;
        int64_t enum_comp = 0;
        int64_t int_comp = 0;
    };

    struct PystoneLiteContext
    {
        int64_t int_glob = 0;
        PystoneLiteRecord ptr_glb_next;
        PystoneLiteRecord ptr_glb;
        PystoneLiteRecord record_template;
    };

    void pystone_lite_copy_record(const PystoneLiteRecord &src,
                                  PystoneLiteRecord &dst)
    {
        dst.ptr_comp = src.ptr_comp;
        dst.discr = src.discr;
        dst.enum_comp = src.enum_comp;
        dst.int_comp = src.int_comp;
    }

    int64_t pystone_lite_proc6(int64_t enum_par_in)
    {
        if(enum_par_in == 1)
        {
            return 2;
        }
        if(enum_par_in == 2)
        {
            return 3;
        }
        if(enum_par_in == 3)
        {
            return 1;
        }
        return 1;
    }

    int64_t pystone_lite_proc7(int64_t int_par_i1, int64_t int_par_i2)
    {
        return int_par_i2 + int_par_i1 + 2;
    }

    PystoneLiteRecord *pystone_lite_proc3(PystoneLiteContext &ctx,
                                          PystoneLiteRecord *ptr_par_out)
    {
        if(ptr_par_out != nullptr)
        {
            ctx.int_glob = pystone_lite_proc7(10, ctx.int_glob);
            return ptr_par_out->ptr_comp;
        }

        return &ctx.ptr_glb;
    }

    int64_t pystone_lite_proc2(PystoneLiteContext &ctx, int64_t int_par_io)
    {
        int64_t int_loc = int_par_io + 10;
        while(int_loc > 3)
        {
            --int_loc;
            ctx.int_glob += int_loc;
        }
        return int_par_io + ctx.int_glob;
    }

    int64_t pystone_lite_proc1(PystoneLiteContext &ctx,
                               PystoneLiteRecord &ptr_par_in)
    {
        PystoneLiteRecord &next_record = *ptr_par_in.ptr_comp;
        pystone_lite_copy_record(ctx.record_template, next_record);
        ptr_par_in.int_comp = 5;
        next_record.int_comp = ptr_par_in.int_comp;
        next_record.ptr_comp = ptr_par_in.ptr_comp;
        next_record.ptr_comp = pystone_lite_proc3(ctx, next_record.ptr_comp);
        if(next_record.discr == 1)
        {
            next_record.int_comp = 6;
            next_record.enum_comp = pystone_lite_proc6(ptr_par_in.enum_comp);
            next_record.int_comp = pystone_lite_proc7(next_record.int_comp, 10);
        }
        else
        {
            pystone_lite_copy_record(next_record, ptr_par_in);
        }
        return ptr_par_in.int_comp + next_record.int_comp +
               next_record.enum_comp;
    }

    int64_t pystone_lite_value(int64_t n)
    {
        PystoneLiteContext ctx;
        ctx.ptr_glb_next = {nullptr, 1, 3, 0};
        ctx.ptr_glb = {&ctx.ptr_glb_next, 1, 3, 40};
        ctx.record_template = {&ctx.ptr_glb, 1, 3, 0};

        int64_t total = 0;
        for(int64_t i = 0; i < n; ++i)
        {
            total += pystone_lite_proc1(ctx, ctx.ptr_glb);
            total += pystone_lite_proc2(ctx, 2);
            total += pystone_lite_proc7(2, 3);

            ctx.ptr_glb.enum_comp = pystone_lite_proc6(ctx.ptr_glb.enum_comp);
            ctx.ptr_glb.int_comp += 1;
            if(ctx.ptr_glb.int_comp > 50)
            {
                ctx.ptr_glb.int_comp -= 7;
            }

            total += ctx.ptr_glb.int_comp;
            total += ctx.ptr_glb.ptr_comp->int_comp;
            total += ctx.int_glob;
        }

        return total + ctx.ptr_glb.enum_comp + ctx.ptr_glb.int_comp +
               ctx.ptr_glb.ptr_comp->int_comp;
    }

    struct PystoneArithmeticContext
    {
        int64_t tag = 0;
        int64_t accum = 0;
    };

    int64_t pystone_arithmetic_proc7(int64_t a, int64_t b) { return a + b + 2; }

    int64_t pystone_arithmetic_proc2(int64_t seed, int64_t limit)
    {
        int64_t total = 0;
        int64_t i = 0;
        while(i < limit)
        {
            total += pystone_arithmetic_proc7(seed, i);
            if(total > 1000)
            {
                total -= 333;
            }
            else
            {
                total += 17;
            }
            ++i;
        }
        return total;
    }

    int64_t pystone_arithmetic_proc6(int64_t tag)
    {
        if(tag == 1)
        {
            return 2;
        }
        if(tag == 2)
        {
            return 3;
        }
        if(tag == 3)
        {
            return 1;
        }
        return 1;
    }

    int64_t pystone_arithmetic_proc1(PystoneArithmeticContext &ctx,
                                     int64_t seed)
    {
        int64_t acc = seed;
        int64_t outer = 0;
        while(outer < 5)
        {
            acc += pystone_arithmetic_proc2(acc + outer, 6);
            if(acc > 5000)
            {
                acc -= 777;
            }
            else
            {
                acc += 111;
            }
            ++outer;
        }
        ctx.tag = pystone_arithmetic_proc6(ctx.tag);
        ctx.accum += acc;
        return acc + ctx.tag;
    }

    int64_t pystone_arithmetic_value(int64_t n)
    {
        PystoneArithmeticContext ctx{1, 0};

        int64_t total = 0;
        int64_t i = 0;
        int64_t seed = 3;
        while(i < n)
        {
            total += pystone_arithmetic_proc1(ctx, seed);
            total += pystone_arithmetic_proc2(seed, 4);
            if(total > 20000)
            {
                total -= 5000;
            }
            else
            {
                total += ctx.tag;
            }

            ++seed;
            if(seed > 8)
            {
                seed = 3;
            }
            ++i;
        }

        return total + ctx.accum + ctx.tag;
    }
}  // namespace

template <typename Program>
static void BM_RecursiveFibonacci(benchmark::State &state)
{
    const int64_t n = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/recursive_fib.py", n,
                                fibonacci_value(n), fibonacci_call_count(n));
}
BENCHMARK_TEMPLATE(BM_RecursiveFibonacci, CloverProgram)
    ->Name("BM_RecursiveFibonacci")
    ->Arg(20)
    ->Arg(25);

template <typename Program> static void BM_WhileLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/while_loop.py", iterations,
                                triangular_number(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_WhileLoop, CloverProgram)
    ->Name("BM_WhileLoop")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program> static void BM_ForLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/for_loop.py", iterations,
                                triangular_number(iterations), iterations);
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
    run_benchmark_case<Program>(state, "benchmark/for_loop_slow_path.py",
                                iterations, triangular_number(iterations),
                                iterations);
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
    run_benchmark_case<Program>(state, "benchmark/nested_for_loop.py",
                                iterations,
                                sum_of_products(iterations, kInnerIterations),
                                iterations * kInnerIterations);
}
BENCHMARK_TEMPLATE(BM_NestedForLoop, CloverProgram)
    ->Name("BM_NestedForLoop")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program>
static void BM_ClassInstantiation(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(
        state, "benchmark/class_instantiation.py", iterations,
        class_instantiation_value(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_ClassInstantiation, CloverProgram)
    ->Name("BM_ClassInstantiation")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program>
static void BM_InstanceAttributeWrite(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(
        state, "benchmark/instance_attribute_write.py", iterations,
        instance_attribute_write_value(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_InstanceAttributeWrite, CloverProgram)
    ->Name("BM_InstanceAttributeWrite")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program> static void BM_MethodCall(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/method_call.py", iterations,
                                method_call_value(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_MethodCall, CloverProgram)
    ->Name("BM_MethodCall")
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

template <typename Program> static void BM_PystoneLite(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(state, "benchmark/pystone_lite.py", iterations,
                                pystone_lite_value(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_PystoneLite, CloverProgram)
    ->Name("BM_PystoneLite")
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000);

template <typename Program>
static void BM_PystoneArithmetic(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    run_benchmark_case<Program>(
        state, "benchmark/pystone_arithmetic.py", iterations,
        pystone_arithmetic_value(iterations), iterations);
}
BENCHMARK_TEMPLATE(BM_PystoneArithmetic, CloverProgram)
    ->Name("BM_PystoneArithmetic")
    ->Arg(100)
    ->Arg(1000)
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
