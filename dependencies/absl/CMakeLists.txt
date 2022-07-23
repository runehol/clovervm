set(ABSL_RUN_TESTS OFF CACHE INTERNAL "")
set(ABSL_ENABLE_INSTALL OFF CACHE INTERNAL "")
set(ABSL_PROPAGATE_CXX_STD ON CACHE INTERNAL "")

FetchContent_GetProperties(absl)
if(NOT absl_POPULATED)
    FetchContent_Populate(absl)
    add_subdirectory(${absl_SOURCE_DIR} ${absl_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
