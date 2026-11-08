add_library(gc_heap_format STATIC heap_format.cpp heap_format.h)
target_link_libraries(gc_heap_format PRIVATE
    nlohmann_json::nlohmann_json project_options project_warnings)
enable_sanitizers(gc_heap_format)

target_sources(test_clovervm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/test_heap_format.cpp")

add_library(gc_heap_runner STATIC heap_runner.cpp heap_runner.h)
target_include_directories(gc_heap_runner PUBLIC
    "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gc_heap_runner
    PUBLIC gc_heap_format absl::flat_hash_map absl::flat_hash_set
    PRIVATE project_options project_warnings)
enable_sanitizers(gc_heap_runner)

target_sources(test_clovervm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/test_heap_runner.cpp")
target_link_libraries(test_clovervm PRIVATE gc_heap_runner)

add_executable(gc_cpp main.cpp)
target_link_libraries(gc_cpp PRIVATE
    gc_heap_runner cxxopts::cxxopts project_options project_warnings)
enable_sanitizers(gc_cpp)

set(gc_cpp_check_command
    "${CMAKE_COMMAND}"
    "-DRUNNER=$<TARGET_FILE:gc_cpp>"
    "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-check"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_cli.cmake")
add_custom_target(check_gc_cpp
    COMMAND ${gc_cpp_check_command}
    DEPENDS gc_cpp
    VERBATIM)
add_test(NAME GCHeapCLI COMMAND ${gc_cpp_check_command})
set_tests_properties(GCHeapCLI PROPERTIES LABELS "regular;full")
