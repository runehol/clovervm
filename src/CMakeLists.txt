find_package(Threads)

set(CL_TRUSTED_BUILTINS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/builtins.py")
set(CL_TRUSTED_SYS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/sys.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CL_TRUSTED_BUILTINS_FILE}"
    "${CL_TRUSTED_SYS_FILE}")
file(READ "${CL_TRUSTED_BUILTINS_FILE}" CL_TRUSTED_BUILTIN_SOURCE)
file(READ "${CL_TRUSTED_SYS_FILE}" CL_TRUSTED_SYS_SOURCE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/builtins.inc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/builtins.inc"
    @ONLY)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/build_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/build_config.h"
    @ONLY)

add_library(libclovervm_objects OBJECT
    attr.cpp
    attr.h
    attribute_cache.h
    attribute_descriptor.h
    ast.h
    ast_print.h
    builtin_class_registry.h
    bytecode.h
    clover_api.cpp
    "${CMAKE_SOURCE_DIR}/include/clovervm/clovervm.h"
    "${CMAKE_SOURCE_DIR}/include/clovervm/native_module.h"
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
    dict_view.cpp
    dict_view.h
    ellipsis_type.cpp
    ellipsis_type.h
    extension_api.cpp
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
    builtins.cpp
    builtins.h
    builtins.py
    builtins.inc.in
    sys.py
    parser.cpp
    parser.h
    range_iterator.cpp
    range_iterator.h
    repl.cpp
    repl.h
    runtime_helpers.h
    heap_reclamation.cpp
    heap_reclamation.h
    shape.cpp
    shape.h
    slot_dict.cpp
    slot_dict.h
    source_text.cpp
    source_text.h
    token.cpp
    token.h
    token_print.h
    tokenizer.h
    tuple.cpp
    tuple.h
    tuple_iterator.cpp
    tuple_iterator.h
    unicode.cpp
    unicode.h
    virtual_machine.cpp
    virtual_machine.h
    list.cpp
    list.h
    list_iterator.cpp
    list_iterator.h
    module_object.cpp
    module_object.h
    module_loader_object.cpp
    module_loader_object.h
    module_finder.cpp
    module_finder.h
    module_spec_object.cpp
    module_spec_object.h
    module_global_cache.h
    native_module_api.cpp
    native_module_api_internal.h
    native_module_loader.cpp
    native_module_loader.h
    native_module_loader_internal.h
    native_module_loader_posix.cpp
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
    not_implemented_type.cpp
    not_implemented_type.h
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

add_library(libclovervm SHARED)
target_link_libraries(libclovervm PRIVATE libclovervm_objects)

set_target_properties(libclovervm_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

set_target_properties(libclovervm PROPERTIES
    OUTPUT_NAME clovervm
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# add project sources to include path
target_include_directories(
    libclovervm_objects PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")
target_include_directories(
    libclovervm_objects PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/")
target_include_directories(
    libclovervm_objects PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_include_directories(
    libclovervm_objects PRIVATE "${PROJECT_BINARY_DIR}/_deps/ctre-src/include")

target_link_libraries(
    libclovervm_objects PRIVATE project_options project_warnings)
target_compile_definitions(libclovervm_objects
    PRIVATE CLOVERVM_BUILDING_SHARED)
target_link_libraries(
    libclovervm_objects PUBLIC absl::inlined_vector absl::flat_hash_map
                              absl::flat_hash_set)
target_link_libraries(libclovervm_objects PUBLIC fmt::fmt)
target_link_libraries(libclovervm_objects PUBLIC ${CMAKE_DL_LIBS})

target_include_directories(libclovervm PUBLIC "${CMAKE_SOURCE_DIR}/include")

clovervm_export_symbols(libclovervm EMBEDDER_API EXTENSION_API)

add_executable(clovervm
    clovervm.cpp
)

set_target_properties(clovervm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_link_libraries(clovervm PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clovervm PRIVATE project_options project_warnings)
target_link_libraries(clovervm PRIVATE libclovervm)
target_include_directories(clovervm PRIVATE "${CMAKE_SOURCE_DIR}/include")
add_dependencies(clovervm clovervm_native_module__math
                          clovervm_native_module__time)

install(TARGETS clovervm DESTINATION bin)

enable_sanitizers(clovervm)
