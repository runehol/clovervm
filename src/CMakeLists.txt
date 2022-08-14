find_package(Threads)


add_executable(clovervm
	clovervm.c
    cl_klass.c
    cl_klass.h
    
    )

# add project sources to include path
target_include_directories(clovervm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")


target_link_libraries(clovervm PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clovervm PRIVATE project_options project_warnings)





install (TARGETS clovervm DESTINATION bin)

enable_sanitizers(clovervm)
