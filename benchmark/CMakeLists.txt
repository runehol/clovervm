find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(bench_clovervm
    bench_interpreter.cpp
    class_attribute_read.cpp
    class_attribute_write.cpp
    class_instantiation.cpp
    class_instantiation_with_init.cpp
    exception_bare_handler_no_raise.cpp
    exception_bare_handler_raise.cpp
    exception_typed_handler_no_raise.cpp
    exception_typed_handler_raise.cpp
    for_loop.cpp
    for_loop_slow_path.cpp
    function_default_varargs.cpp
    function_default_parameter.cpp
    function_varargs.cpp
    function_varargs_with_positional.cpp
    instance_attribute_add_after_construction.cpp
    instance_attribute_add_member.cpp
    instance_attribute_read.cpp
    instance_attribute_write.cpp
    method_call.cpp
    method_call_class_attribute_write.cpp
    nested_for_loop.cpp
    pystone_arithmetic.cpp
    pystone_lite.cpp
    recursive_fib.cpp
    while_loop.cpp
)

set_target_properties(bench_clovervm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

enable_sanitizers(bench_clovervm)

target_link_libraries(bench_clovervm PRIVATE benchmark::benchmark)
target_link_libraries(bench_clovervm PRIVATE libclovervm)
target_link_libraries(bench_clovervm PRIVATE project_options project_warnings)
target_compile_definitions(bench_clovervm
    PRIVATE CLOVERVM_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_custom_target(check_opcode_frames
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tools/check_opcode_frames.py
            --binary $<TARGET_FILE:clovervm>
            --required-list
            ${PROJECT_SOURCE_DIR}/benchmark/hot_path_opcode_handlers.txt
            --nm ${CMAKE_NM}
            --objdump ${CMAKE_OBJDUMP}
            --warn-only
    DEPENDS clovervm
    USES_TERMINAL
)

add_custom_target(run_benchmark
    COMMAND $<TARGET_FILE:bench_clovervm>
            --benchmark_min_time=0.1s
            --benchmark_counters_tabular=true
    DEPENDS check_opcode_frames bench_clovervm
    USES_TERMINAL
)
