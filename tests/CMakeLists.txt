include( CTest )

include(GoogleTest)

add_executable(test_clovervm)

set_target_properties(test_clovervm PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

enable_sanitizers(test_clovervm)

target_sources(test_clovervm PRIVATE
	test_codegen.cpp
	test_exception_handling.cpp
	test_heap.cpp
	test_import_system.cpp
	test_native_module_build.cpp
	test_python_source_files.cpp
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

clovervm_add_native_module(_test_native
	SOURCES
		native_modules/_test_native.c)
add_dependencies(test_clovervm clovervm_native_module__test_native)
clovervm_add_native_module(_test_native_missing_symbol
	SOURCES
		native_modules/_test_native_missing_symbol.c)
add_dependencies(test_clovervm
	clovervm_native_module__test_native_missing_symbol)

add_custom_target(check_native_module_linkage
	COMMAND ${CMAKE_COMMAND}
		-DMODULE_PATH=$<TARGET_FILE:clovervm_native_module__test_native>
		-DFORBIDDEN_LIBRARY=$<TARGET_FILE_NAME:libclovervm>
		-DOBJDUMP=${CMAKE_OBJDUMP}
		-DPLATFORM_NAME=${CMAKE_SYSTEM_NAME}
		-P ${PROJECT_SOURCE_DIR}/cmake/CheckNativeModuleLinkage.cmake
	DEPENDS clovervm_native_module__test_native
	USES_TERMINAL
)

gtest_discover_tests(test_clovervm
	WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
	DISCOVERY_MODE PRE_TEST)

add_custom_target(check
COMMAND test_clovervm --gtest_brief=1
DEPENDS test_clovervm check_native_module_linkage
WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
USES_TERMINAL
)
