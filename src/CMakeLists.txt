find_package(Threads)


add_executable(clojit
	clojit.c
    )

# add project sources to include path
target_include_directories(clojit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")


target_link_libraries(clojit PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clojit PRIVATE project_options project_warnings)





install (TARGETS clojit DESTINATION bin)

enable_sanitizers(clojit)
