find_package(Threads)


add_library(libclovervm STATIC
	clovervm.c
    cl_klass.c
    cl_klass.h
    cl_object.h
    cl_persistent_list.c
    cl_persistent_list.h
    cl_refcount.h
    cl_short_vector.c
    cl_short_vector.h
    cl_string.c
    cl_string.h
    cl_value.h
    tokenizer.cpp
	tokenizer.h
	token.cpp
	token.h
	compilation_unit.h

    )

# add project sources to include path
target_include_directories(libclovervm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")


target_link_libraries(libclovervm PRIVATE project_options project_warnings)
target_link_libraries(libclovervm PRIVATE absl::inlined_vector absl::flat_hash_map absl::flat_hash_set)
target_link_libraries(libclovervm PRIVATE ctre)


add_executable(clovervm
	clovervm.c
	)

target_link_libraries(clovervm PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clovervm PRIVATE project_options project_warnings)
target_link_libraries(clovervm PRIVATE libclovervm)


install (TARGETS clovervm DESTINATION bin)

enable_sanitizers(clovervm)
