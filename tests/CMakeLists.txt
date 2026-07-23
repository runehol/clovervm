include( CTest )

include(GoogleTest)

add_executable(test_clovervm)

set_target_properties(test_clovervm PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

enable_sanitizers(test_clovervm)

target_sources(test_clovervm PRIVATE
	test_bigint.cpp
	test_bytecode.cpp
	test_codegen.cpp
	test_exception_handling.cpp
	test_hash.cpp
	test_heap.cpp
	test_import_system.cpp
	test_aarch64_assembler.cpp
	test_code_cache.cpp
	test_jit_allocation_constraints.cpp
	test_jit_arena.cpp
	test_jit_cfg.cpp
	test_jit_graph_rewrites.cpp
	test_machine_address.cpp
	test_machine_code_emitter.cpp
	test_native_module_build.cpp
	test_python_source_files.cpp
	test_result.cpp
	test_attr.cpp
	test_dict.cpp
	test_parser.cpp
	test_tokenizer.cpp
	test_interpreter.cpp
	test_list.cpp
	test_module_global.cpp
	test_module_object.cpp
	test_native_layout_descriptor.cpp
	test_shape.cpp
	test_scope.cpp
	test_heap_reclamation.cpp
	test_thread_state.cpp
	test_owned.cpp
	test_typed_value.cpp
	test_unicode.cpp
	test_tuple.cpp
	)

target_link_libraries(test_clovervm PRIVATE gtest_main)
target_link_libraries(test_clovervm PRIVATE libclovervm_objects)
target_link_libraries(test_clovervm PRIVATE project_options project_warnings)
clovervm_export_symbols(test_clovervm EXTENSION_API)
add_dependencies(test_clovervm clovervm_native_module__math
                             clovervm_native_module__os
                             clovervm_native_module__time)

set(CLOVERVM_TEST_EXTENSION_API_SYMBOLS
	"${CMAKE_CURRENT_BINARY_DIR}/clovervm_extension_api_symbols.txt")
clovervm_write_symbol_list("${CLOVERVM_TEST_EXTENSION_API_SYMBOLS}"
	EXTENSION_API)

clovervm_add_native_module(_test_native
	SOURCES
		native_modules/_test_native.c)
add_dependencies(test_clovervm clovervm_native_module__test_native)
clovervm_add_native_module(_test_native_missing_symbol
	SOURCES
		native_modules/_test_native_missing_symbol.c)
add_dependencies(test_clovervm
	clovervm_native_module__test_native_missing_symbol)
clovervm_add_native_module(_test_native_bad_value
	SOURCES
		native_modules/_test_native_bad_value.c)
add_dependencies(test_clovervm
	clovervm_native_module__test_native_bad_value)
clovervm_add_native_module(_test_native_fail_no_exception
	SOURCES
		native_modules/_test_native_fail_no_exception.c)
add_dependencies(test_clovervm
	clovervm_native_module__test_native_fail_no_exception)
clovervm_add_native_module(_test_native_success_with_exception
	SOURCES
		native_modules/_test_native_success_with_exception.c)
add_dependencies(test_clovervm
	clovervm_native_module__test_native_success_with_exception)

add_custom_target(check_native_module_linkage
	COMMAND ${CMAKE_COMMAND}
		-DMODULE_PATH=$<TARGET_FILE:clovervm_native_module__test_native>
		-DFORBIDDEN_LIBRARY=$<TARGET_FILE_NAME:libclovervm>
		-DALLOWED_UNDEFINED_SYMBOLS_FILE=${CLOVERVM_TEST_EXTENSION_API_SYMBOLS}
		-DNM=${CMAKE_NM}
		-DOBJDUMP=${CMAKE_OBJDUMP}
		-DPLATFORM_NAME=${CMAKE_SYSTEM_NAME}
		-P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeModuleLinkage.cmake
	DEPENDS clovervm_native_module__test_native
	USES_TERMINAL
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
	target_sources(test_clovervm PRIVATE test_aarch64_execution.cpp)
endif()

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
	target_sources(test_clovervm PRIVATE test_map_jit.cpp)
endif()

gtest_discover_tests(test_clovervm
	WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
	DISCOVERY_MODE PRE_TEST)

add_custom_target(check
COMMAND test_clovervm --gtest_brief=1
COMMAND clovervm -c "assert 1 + 2 == 3"
DEPENDS test_clovervm clovervm check_native_module_linkage
WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
USES_TERMINAL
)
