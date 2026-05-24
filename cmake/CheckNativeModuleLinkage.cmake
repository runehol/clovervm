if(NOT DEFINED MODULE_PATH)
    message(FATAL_ERROR "MODULE_PATH is required")
endif()
if(NOT DEFINED FORBIDDEN_LIBRARY)
    message(FATAL_ERROR "FORBIDDEN_LIBRARY is required")
endif()

if(PLATFORM_NAME STREQUAL "Darwin")
    find_program(OTOOL_EXECUTABLE otool)
    if(NOT OTOOL_EXECUTABLE)
        message(FATAL_ERROR "otool is required to inspect native module linkage")
    endif()
    execute_process(
        COMMAND "${OTOOL_EXECUTABLE}" -L "${MODULE_PATH}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
elseif(UNIX)
    if(NOT DEFINED OBJDUMP OR NOT OBJDUMP)
        message(FATAL_ERROR "OBJDUMP is required to inspect native module linkage")
    endif()
    execute_process(
        COMMAND "${OBJDUMP}" -p "${MODULE_PATH}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
else()
    return()
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "Failed to inspect native module linkage for ${MODULE_PATH}:\n"
            "${error}")
endif()

string(FIND "${output}" "${FORBIDDEN_LIBRARY}" forbidden_index)
if(NOT forbidden_index EQUAL -1)
    message(FATAL_ERROR
            "Native module ${MODULE_PATH} unexpectedly links "
            "${FORBIDDEN_LIBRARY}:\n${output}")
endif()
