include(CMakeParseArguments)

if(APPLE)
    set(CL_NATIVE_MODULE_SUFFIX ".clover.dylib")
elseif(WIN32)
    set(CL_NATIVE_MODULE_SUFFIX ".clover.dll")
else()
    set(CL_NATIVE_MODULE_SUFFIX ".clover.so")
endif()

function(clovervm_add_native_module module_name)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "Unexpected arguments to clovervm_add_native_module: "
                "${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR
                "clovervm_add_native_module(${module_name}) requires SOURCES")
    endif()

    string(REPLACE "." "/" module_relative_path "${module_name}")
    get_filename_component(module_output_subdir "${module_relative_path}"
                           DIRECTORY)
    get_filename_component(module_leaf_name "${module_relative_path}" NAME)

    string(MAKE_C_IDENTIFIER "${module_name}" module_identifier)
    set(target_name "clovervm_native_module_${module_identifier}")

    add_library(${target_name} MODULE ${ARG_SOURCES})
    target_include_directories(${target_name}
        PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(${target_name}
        PRIVATE project_options project_warnings)

    if(APPLE)
        target_link_options(${target_name}
            PRIVATE "LINKER:-undefined,dynamic_lookup")
    endif()

    set(module_output_dir "${CL_BUILD_STDLIB_DIR}")
    if(module_output_subdir)
        set(module_output_dir
            "${CL_BUILD_STDLIB_DIR}/${module_output_subdir}")
    endif()

    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${module_leaf_name}"
        SUFFIX "${CL_NATIVE_MODULE_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY "${module_output_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${module_output_dir}"
        ARCHIVE_OUTPUT_DIRECTORY "${module_output_dir}"
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    enable_sanitizers(${target_name})

    if(NOT TARGET clovervm_native_modules)
        add_custom_target(clovervm_native_modules)
    endif()
    add_dependencies(clovervm_native_modules ${target_name})
endfunction()
