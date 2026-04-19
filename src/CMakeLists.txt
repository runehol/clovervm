find_package(Threads)

add_library(libclovervm STATIC
    attr.cpp
    attr.h
    ast.h
    ast_print.h
    bytecode.h
    code_object.h
    class_object.cpp
    class_object.h
    codegen.cpp
    codegen.h
    compilation_unit.h
    heap.cpp
    heap.h
    indirect_dict.cpp
    indirect_dict.h
    instance.cpp
    instance.h
    interpreter.cpp
    parser.cpp
    parser.h
    range_iterator.h
    shape.cpp
    shape.h
    token.cpp
    token.h
    token_print.h
    tokenizer.h
    virtual_machine.cpp
    virtual_machine.h
    klass.cpp
    klass.h
    list.cpp
    list.h
    object.h
    owned.h
    owned_typed_value.h
    refcount.cpp
    refcount.h
    scope.cpp
    scope.h
    slab_allocator.cpp
    slab_allocator.h
    str.cpp
    str.h
    subscript.cpp
    subscript.h
    thread_state.cpp
    thread_state.h
    typed_value.h
    tokenizer.cpp
    value.h
    vm_array.h
)

set_target_properties(libclovervm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# add project sources to include path
target_include_directories(libclovervm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")
target_include_directories(
    libclovervm PRIVATE "${PROJECT_BINARY_DIR}/_deps/ctre-src/include")

target_link_libraries(libclovervm PRIVATE project_options project_warnings)
target_link_libraries(
    libclovervm PUBLIC absl::inlined_vector absl::flat_hash_map
                     absl::flat_hash_set)
target_link_libraries(libclovervm PUBLIC fmt::fmt)

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

install(TARGETS clovervm DESTINATION bin)

enable_sanitizers(clovervm)
