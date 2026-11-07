add_library(gc_heap_format STATIC heap_format.cpp heap_format.h)
target_link_libraries(gc_heap_format PRIVATE
    nlohmann_json::nlohmann_json project_options project_warnings)
enable_sanitizers(gc_heap_format)

target_sources(test_clovervm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/test_heap_format.cpp")
target_link_libraries(test_clovervm PRIVATE gc_heap_format)
