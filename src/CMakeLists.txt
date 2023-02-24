find_package(Threads)


add_library(libclovervm STATIC
	bytecode.h
	code_object.h
	codegen.cpp
	codegen.h
    klass.cpp
    klass.h
    object.h
    refcount.h
    short_vector.cpp
    short_vector.h
    str.cpp
    str.h
    value.h
	ast.h
	ast_print.h
	parser.cpp
	parser.h
    tokenizer.cpp
	tokenizer.h
	token.cpp
	token.h
	token_print.h
	compilation_unit.h
	interpreter.cpp

    )

# add project sources to include path
target_include_directories(libclovervm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")


target_link_libraries(libclovervm PRIVATE project_options project_warnings)
target_link_libraries(libclovervm PUBLIC absl::inlined_vector absl::flat_hash_map absl::flat_hash_set)
target_link_libraries(libclovervm PUBLIC fmt)
target_link_libraries(libclovervm PRIVATE ctre)


add_executable(clovervm
	clovervm.cpp
	)

target_link_libraries(clovervm PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clovervm PRIVATE project_options project_warnings)
target_link_libraries(clovervm PRIVATE libclovervm)


install (TARGETS clovervm DESTINATION bin)

enable_sanitizers(clovervm)
