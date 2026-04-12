add_executable(bench_interpreter
    bench_interpreter.cpp
)

set_target_properties(bench_interpreter PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

enable_sanitizers(bench_interpreter)

target_link_libraries(bench_interpreter PRIVATE benchmark::benchmark)
target_link_libraries(bench_interpreter PRIVATE libclovervm)
target_link_libraries(bench_interpreter PRIVATE project_options project_warnings)
target_compile_definitions(bench_interpreter
    PRIVATE CLOVERVM_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_custom_target(run_benchmark
    COMMAND bench_interpreter --benchmark_min_time=0.1s --benchmark_counters_tabular=true
    DEPENDS bench_interpreter
    USES_TERMINAL
)
