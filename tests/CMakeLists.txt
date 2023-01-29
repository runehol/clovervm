include( CTest )

include(GoogleTest)

add_executable(test_clovervm)

enable_sanitizers(test_clovervm)

target_sources(test_clovervm PRIVATE
	test_tokenizer.cpp
	)

target_link_libraries(test_clovervm PRIVATE gtest_main)
target_link_libraries(test_clovervm PRIVATE libclovervm)
target_link_libraries(test_clovervm PRIVATE project_options project_warnings)

gtest_discover_tests(test_clovervm WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

set(CMAKE_CTEST_COMMAND ctest --progress --output-on-failure)

add_custom_target(check
COMMAND ${CMAKE_CTEST_COMMAND}
DEPENDS test_clovervm
USES_TERMINAL
)
