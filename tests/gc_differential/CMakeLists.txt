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
