find_package(Threads)

set(CL_TRUSTED_BUILTINS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/builtins.py")
set(CL_TRUSTED_SYS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/sys.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CL_TRUSTED_BUILTINS_FILE}"
    "${CL_TRUSTED_SYS_FILE}")
file(READ "${CL_TRUSTED_BUILTINS_FILE}" CL_TRUSTED_BUILTIN_SOURCE)
file(READ "${CL_TRUSTED_SYS_FILE}" CL_TRUSTED_SYS_SOURCE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/builtins.inc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/builtins.inc"
    @ONLY)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/build_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/build_config.h"
    @ONLY)

add_library(libclovervm_objects OBJECT
    object_model/attr.cpp
    object_model/attr.h
    object_model/attribute_cache.h
    object_model/attribute_descriptor.h
    compiler/ast.h
    compiler/ast_print.h
    object_model/builtin_class_registry.h
    bytecode/bytecode.h
    bytecode/bytecode_decoder.cpp
    bytecode/bytecode_decoder.h
    bytecode/bytecode_instruction.cpp
    bytecode/bytecode_instruction.h
    api/clover_api.cpp
    "${CMAKE_SOURCE_DIR}/include/clovervm/clovervm.h"
    "${CMAKE_SOURCE_DIR}/include/clovervm/native_module.h"
    bytecode/code_object.cpp
    bytecode/code_object.h
    bytecode/code_object_builder.cpp
    bytecode/code_object_builder.h
    bytecode/code_object_print.h
    object_model/class_object.cpp
    object_model/class_object.h
    object_model/constructor_thunk.cpp
    object_model/constructor_thunk.h
    compiler/codegen.cpp
    compiler/codegen.h
    compiler/codegen_scope_analysis.cpp
    compiler/codegen_scope_analysis.h
    compiler/compilation_unit.h
    builtin_types/bigint.cpp
    builtin_types/bigint.h
    builtin_types/bytes.cpp
    builtin_types/bytes.h
    builtin_types/dict.cpp
    builtin_types/dict.h
    builtin_types/dict_view.cpp
    builtin_types/dict_view.h
    builtin_types/ellipsis_type.cpp
    builtin_types/ellipsis_type.h
    api/extension_api.cpp
    api/extension_handle.cpp
    api/extension_handle.h
    api/native_module_api.cpp
    api/native_module_api_internal.h
    runtime/exception_handling.h
    runtime/exception_object.cpp
    runtime/exception_object.h
    runtime/exception_propagation.h
    runtime/fatal.cpp
    runtime/fatal.h
    util/compiler.h
    util/result.h
    builtin_types/float.cpp
    builtin_types/float.h
    builtin_types/hash.cpp
    builtin_types/hash.h
    object_model/function.cpp
    object_model/function.h
    memory/global_heap.cpp
    memory/global_heap.h
    memory/heap_constants.h
    memory/heap_construction.h
    object_model/heap_object.h
    object_model/instance.cpp
    object_model/instance.h
    builtin_types/int.cpp
    builtin_types/int.h
    builtin_types/intern_store.h
    import_system/import_system.cpp
    import_system/import_system.h
    jit/aarch64_assembler.cpp
    jit/aarch64_assembler.h
    jit/aarch64_cfg_emitter.cpp
    jit/aarch64_cfg_emitter.h
    jit/cfg_verifier.cpp
    jit/cfg_verifier.h
    jit/compilation_arena.cpp
    jit/compilation_arena.h
    jit/control_flow_graph.cpp
    jit/control_flow_graph.h
    jit/graph_builder.cpp
    jit/graph_builder.h
    jit/graph_queries.cpp
    jit/graph_queries.h
    jit/code_cache.cpp
    jit/code_cache.h
    jit/instruction.cpp
    jit/instruction.h
    jit/instruction_traversal.h
    jit/instruction_pool.cpp
    jit/instruction_pool.h
    jit/code_cache_types.h
    jit/machine_address.h
    jit/machine_address_internal.h
    jit/machine_code_emitter.h
    jit/object_pool.h
    jit/platform_code_memory.h
    jit/preferred_code_memory.cpp
    jit/serial.h
    jit/standard_code_memory.cpp
    jit/standard_code_memory.h
    jit/use_lists.cpp
    jit/use_lists.h
    runtime/interpreter.cpp
    runtime/interpreter.h
    builtin_types/bool.cpp
    builtin_types/bool.h
    bootstrap/builtins.cpp
    bootstrap/builtins.h
    bootstrap/builtins.py
    bootstrap/builtins.inc.in
    bootstrap/sys.py
    compiler/parser.cpp
    compiler/parser.h
    builtin_types/range_iterator.cpp
    builtin_types/range_iterator.h
    cli/repl.cpp
    cli/repl.h
    runtime/method_call.h
    runtime/runtime_helpers.h
    memory/heap_reclamation.cpp
    memory/heap_reclamation.h
    object_model/shape.cpp
    object_model/shape.h
    object_model/slot_dict.cpp
    object_model/slot_dict.h
    builtin_types/slice.cpp
    builtin_types/slice.h
    compiler/source_text.cpp
    compiler/source_text.h
    compiler/token.cpp
    compiler/token.h
    compiler/token_print.h
    compiler/tokenizer.h
    builtin_types/tuple.cpp
    builtin_types/tuple.h
    builtin_types/tuple_iterator.cpp
    builtin_types/tuple_iterator.h
    builtin_types/unicode.cpp
    builtin_types/unicode.h
    runtime/virtual_machine.cpp
    runtime/virtual_machine.h
    builtin_types/list.cpp
    builtin_types/list.h
    builtin_types/list_iterator.cpp
    builtin_types/list_iterator.h
    builtin_types/module_object.cpp
    builtin_types/module_object.h
    builtin_types/module_loader_object.cpp
    builtin_types/module_loader_object.h
    import_system/module_finder.cpp
    import_system/module_finder.h
    builtin_types/module_spec_object.cpp
    builtin_types/module_spec_object.h
    import_system/module_global_cache.h
    import_system/native_extension_loader.cpp
    import_system/native_extension_loader.h
    import_system/native_extension_loader_internal.h
    import_system/native_extension_loader_posix.cpp
    import_system/module_global.cpp
    import_system/module_global.h
    import_system/module_global_descriptor.h
    object_model/native_function.cpp
    object_model/native_function.h
    memory/native_layout_declarations.h
    memory/native_layout_descriptor.cpp
    memory/native_layout_descriptor.h
    memory/native_layout_id.h
    memory/native_layout_registry.h
    builtin_types/none_type.cpp
    builtin_types/none_type.h
    builtin_types/not_implemented_type.cpp
    builtin_types/not_implemented_type.h
    object_model/object.cpp
    object_model/object.h
    object_model/overflow_slots.cpp
    object_model/overflow_slots.h
    object_model/owned.h
    object_model/refcount.cpp
    object_model/refcount.h
    runtime/operator_dispatch.h
    runtime/operator_walk.cpp
    runtime/operator_walk.h
    runtime/protocol_helper_functions.cpp
    runtime/protocol_helper_functions.h
    compiler/scope.cpp
    compiler/scope.h
    object_model/shape_descriptor.h
    memory/slab_allocator.cpp
    memory/slab_allocator.h
    api/clover_entry.cpp
    api/clover_entry.h
    builtin_types/str.cpp
    builtin_types/str.h
    memory/thread_local_heap.cpp
    memory/thread_local_heap.h
    runtime/thread_state.cpp
    runtime/thread_state.h
    object_model/typed_value.cpp
    object_model/typed_value.h
    compiler/tokenizer.cpp
    object_model/validity_cell.h
    object_model/value.h
    object_model/value_string.cpp
    object_model/value_string.h
    builtin_types/string_builder.cpp
    builtin_types/string_builder.h
    object_model/vm_array_backing.h
    object_model/vm_array.h
)

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    target_sources(libclovervm_objects PRIVATE
        jit/map_jit_code_memory.cpp
        jit/map_jit_code_memory.h)
endif()

add_library(libclovervm SHARED)
target_link_libraries(libclovervm PRIVATE libclovervm_objects)

set_target_properties(libclovervm_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

set_target_properties(libclovervm PROPERTIES
    OUTPUT_NAME clovervm
    CXX_STANDARD 20
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
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(libclovervm_objects PRIVATE -fno-exceptions)
else()
    message(FATAL_ERROR
        "libclovervm no-exceptions build is not configured for this compiler")
endif()
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
    cli/clovervm.cpp
)

set_target_properties(clovervm PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_link_libraries(clovervm PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clovervm PRIVATE project_options project_warnings)
target_link_libraries(clovervm PRIVATE cxxopts::cxxopts)
target_link_libraries(clovervm PRIVATE libclovervm)
target_include_directories(clovervm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(clovervm PRIVATE "${CMAKE_SOURCE_DIR}/include")
add_dependencies(clovervm clovervm_native_module__math
                          clovervm_native_module__time)

install(TARGETS clovervm DESTINATION bin)

enable_sanitizers(clovervm)
