find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(bench_clovervm
    bench_interpreter.cpp
    builtin_lookup.cpp
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
    function_keyword.cpp
    function_keyword_default.cpp
    function_keyword_mixed.cpp
    function_varargs.cpp
    function_varargs_with_positional.cpp
    getslice.cpp
    getitem_dict.cpp
    getitem_list.cpp
    getitem_str.cpp
    getitem_tuple.cpp
    getitem_user.cpp
    setitem_dict.cpp
    setitem_list.cpp
    setitem_user.cpp
    global_add_delete.cpp
    global_read.cpp
    global_refcounted_write.cpp
    global_write.cpp
    instance_attribute_add_after_construction.cpp
    instance_attribute_add_member.cpp
    instance_attribute_read.cpp
    instance_attribute_refcounted_write.cpp
    instance_attribute_write.cpp
    int_constructor_int.cpp
    int_constructor_string.cpp
    method_call.cpp
    method_call_class_attribute_write.cpp
    method_keyword_call.cpp
    memory_reclamation.cpp
    nbody.cpp
    nested_for_loop.cpp
    pystone_arithmetic.cpp
    pystone_lite.cpp
    recursive_fib.cpp
    str_constructor_int.cpp
    str_constructor_string.cpp
    while_loop.cpp
)

set_target_properties(bench_clovervm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

enable_sanitizers(bench_clovervm)

target_link_libraries(bench_clovervm PRIVATE benchmark::benchmark)
target_link_libraries(bench_clovervm PRIVATE libclovervm_objects)
target_link_libraries(bench_clovervm PRIVATE project_options project_warnings)
clovervm_export_symbols(bench_clovervm EXTENSION_API)
target_compile_definitions(bench_clovervm
    PRIVATE CLOVERVM_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

set(CLOVERVM_INTERPRETER_OBJECT
    "$<FILTER:$<TARGET_OBJECTS:libclovervm_objects>,INCLUDE,.*/interpreter\\.cpp\\.(o|obj)>")

add_custom_target(check_opcode_frames
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tools/check_opcode_frames.py
            --binary ${CLOVERVM_INTERPRETER_OBJECT}
            --required-list
            ${PROJECT_SOURCE_DIR}/benchmark/hot_path_opcode_handlers.txt
            --nm ${CMAKE_NM}
            --objdump ${CMAKE_OBJDUMP}
            --warn-only
    DEPENDS libclovervm_objects
    USES_TERMINAL
)

add_custom_target(run_benchmark
    COMMAND $<TARGET_FILE:bench_clovervm>
            --benchmark_min_time=1s
            --benchmark_counters_tabular=true
    DEPENDS check_opcode_frames bench_clovervm
    USES_TERMINAL
)
