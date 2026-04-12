#include <benchmark/benchmark.h>

#include "code_object.h"
#include "parser.h"
#include "thread_state.h"
#include "value.h"
#include "virtual_machine.h"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
    using namespace cl;

    class CompiledProgram
    {
    public:
        explicit CompiledProgram(const std::wstring &source)
            : thread_(vm_.get_default_thread()),
              code_(thread_->compile(source.c_str(), StartRule::File))
        {
        }

        Value run() { return thread_->run(code_); }

    private:
        VirtualMachine vm_;
        ThreadState *thread_;
        CodeObject *code_;
    };

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

    void expect_smi(Value actual, int64_t expected)
    {
        if(!actual.is_smi() || actual.get_smi() != expected)
        {
            throw std::runtime_error("benchmark produced unexpected result");
        }
    }

    std::wstring load_program_source(const char *relative_path, int64_t n)
    {
        std::ifstream input(std::string(CLOVERVM_SOURCE_DIR) + "/" +
                            relative_path);
        if(!input)
        {
            throw std::runtime_error("failed to open benchmark source file");
        }

        std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        const std::string needle = "__N__";
        const std::string replacement = std::to_string(n);
        size_t pos = 0;
        while((pos = source.find(needle, pos)) != std::string::npos)
        {
            source.replace(pos, needle.size(), replacement);
            pos += replacement.size();
        }

        return std::wstring(source.begin(), source.end());
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
}  // namespace

static void BM_RecursiveFibonacci(benchmark::State &state)
{
    const int64_t n = state.range(0);
    CompiledProgram program(
        load_program_source("benchmark/interpreter_recursive_fib.py", n));
    expect_smi(program.run(), fibonacci_value(n));

    for(auto _: state)
    {
        benchmark::DoNotOptimize(program.run());
    }

    state.SetItemsProcessed(state.iterations() * fibonacci_call_count(n));
}
BENCHMARK(BM_RecursiveFibonacci)->Arg(20)->Arg(25);

static void BM_WhileLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    CompiledProgram program(
        load_program_source("benchmark/interpreter_while_loop.py", iterations));
    expect_smi(program.run(), triangular_number(iterations));

    for(auto _: state)
    {
        benchmark::DoNotOptimize(program.run());
    }

    state.SetItemsProcessed(state.iterations() * iterations);
}
BENCHMARK(BM_WhileLoop)->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_ForLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    CompiledProgram program(
        load_program_source("benchmark/interpreter_for_loop.py", iterations));
    expect_smi(program.run(), triangular_number(iterations));

    for(auto _: state)
    {
        benchmark::DoNotOptimize(program.run());
    }

    state.SetItemsProcessed(state.iterations() * iterations);
}
BENCHMARK(BM_ForLoop)->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_NestedForLoop(benchmark::State &state)
{
    const int64_t iterations = state.range(0);
    static constexpr int64_t kInnerIterations = 10;
    CompiledProgram program(load_program_source(
        "benchmark/interpreter_nested_for_loop.py", iterations));
    expect_smi(program.run(), sum_of_products(iterations, kInnerIterations));

    for(auto _: state)
    {
        benchmark::DoNotOptimize(program.run());
    }

    state.SetItemsProcessed(state.iterations() * iterations * kInnerIterations);
}
BENCHMARK(BM_NestedForLoop)->Arg(1000)->Arg(10000)->Arg(100000);

BENCHMARK_MAIN();
