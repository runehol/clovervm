find_package(Threads)

set(CL_TRUSTED_BUILTINS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/builtins.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CL_TRUSTED_BUILTINS_FILE}")
file(READ "${CL_TRUSTED_BUILTINS_FILE}" CL_TRUSTED_BUILTIN_SOURCE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/builtins.inc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/builtins.inc"
    @ONLY)

set(CL_STDLIB_DIR "${CMAKE_SOURCE_DIR}/stdlib")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/build_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/build_config.h"
    @ONLY)

add_library(libclovervm STATIC
    attr.cpp
    attr.h
    attribute_cache.h
    attribute_descriptor.h
    ast.h
    ast_print.h
    builtin_class_registry.h
    bytecode.h
    code_object.cpp
    code_object.h
    code_object_builder.cpp
    code_object_builder.h
    class_object.cpp
    class_object.h
    constructor_thunk.cpp
    constructor_thunk.h
    codegen.cpp
    codegen.h
    codegen_scope_analysis.cpp
    codegen_scope_analysis.h
    compilation_unit.h
    dict.cpp
    dict.h
    exception_handling.h
    exception_object.cpp
    exception_object.h
    exception_propagation.h
    float.cpp
    float.h
    function.cpp
    global_heap.cpp
    global_heap.h
    heap_construction.h
    heap_object.h
    instance.cpp
    instance.h
    int.cpp
    int.h
    import_system.cpp
    import_system.h
    interpreter.cpp
    bool.cpp
    bool.h
    builtins.py
    builtins.inc.in
    parser.cpp
    parser.h
    range_iterator.cpp
    range_iterator.h
    runtime_helpers.h
    heap_reclamation.cpp
    heap_reclamation.h
    shape.cpp
    shape.h
    slot_dict.cpp
    slot_dict.h
    token.cpp
    token.h
    token_print.h
    tokenizer.h
    tuple.cpp
    tuple.h
    tuple_iterator.cpp
    tuple_iterator.h
    virtual_machine.cpp
    virtual_machine.h
    list.cpp
    list.h
    list_iterator.cpp
    list_iterator.h
    module_object.cpp
    module_object.h
    module_global_cache.h
    module_global.cpp
    module_global.h
    module_global_descriptor.h
    native_function.cpp
    native_function.h
    native_layout_declarations.h
    native_layout_descriptor.cpp
    native_layout_descriptor.h
    native_layout_id.h
    native_layout_registry.h
    none_type.cpp
    none_type.h
    object.cpp
    object.h
    overflow_slots.cpp
    overflow_slots.h
    owned.h
    refcount.cpp
    refcount.h
    scope.cpp
    scope.h
    shape_descriptor.h
    slab_allocator.cpp
    slab_allocator.h
    clover_entry.cpp
    clover_entry.h
    str.cpp
    str.h
    subscript.cpp
    subscript.h
    thread_local_heap.cpp
    thread_local_heap.h
    thread_state.cpp
    thread_state.h
    typed_value.cpp
    typed_value.h
    tokenizer.cpp
    validity_cell.h
    value.h
    value_string.cpp
    value_string.h
    string_builder.cpp
    string_builder.h
    vm_array.h
)

set_target_properties(libclovervm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# add project sources to include path
target_include_directories(libclovervm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")
target_include_directories(libclovervm PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/")
target_include_directories(
    libclovervm PRIVATE "${PROJECT_BINARY_DIR}/_deps/ctre-src/include")

target_link_libraries(libclovervm PRIVATE project_options project_warnings)
target_link_libraries(
    libclovervm PUBLIC absl::inlined_vector absl::flat_hash_map
                     absl::flat_hash_set)
target_link_libraries(libclovervm PUBLIC fmt::fmt)

add_executable(clovervm
    clovervm.cpp
    repl.cpp
    repl.h
)

set_target_properties(clovervm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_link_libraries(clovervm PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clovervm PRIVATE project_options project_warnings)
target_link_libraries(clovervm PRIVATE libclovervm)

install(TARGETS clovervm DESTINATION bin)

enable_sanitizers(clovervm)
