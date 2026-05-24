if(NOT DEFINED MODULE_PATH)
    message(FATAL_ERROR "MODULE_PATH is required")
endif()
if(NOT DEFINED FORBIDDEN_LIBRARY)
    message(FATAL_ERROR "FORBIDDEN_LIBRARY is required")
endif()
if(DEFINED ALLOWED_UNDEFINED_SYMBOLS_FILE AND
   NOT EXISTS "${ALLOWED_UNDEFINED_SYMBOLS_FILE}")
    message(FATAL_ERROR
            "ALLOWED_UNDEFINED_SYMBOLS_FILE does not exist: "
            "${ALLOWED_UNDEFINED_SYMBOLS_FILE}")
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

if(DEFINED ALLOWED_UNDEFINED_SYMBOLS_FILE)
    if(NOT DEFINED NM OR NOT NM)
        message(FATAL_ERROR
                "NM is required to inspect native module undefined symbols")
    endif()

    file(STRINGS "${ALLOWED_UNDEFINED_SYMBOLS_FILE}" allowed_symbols)
    execute_process(
        COMMAND "${NM}" -u -g "${MODULE_PATH}"
        RESULT_VARIABLE nm_result
        OUTPUT_VARIABLE nm_output
        ERROR_VARIABLE nm_error)
    if(NOT nm_result EQUAL 0)
        message(FATAL_ERROR
                "Failed to inspect native module undefined symbols for "
                "${MODULE_PATH}:\n${nm_error}")
    endif()

    string(REPLACE "\n" ";" undefined_lines "${nm_output}")
    set(disallowed_symbols)
    foreach(line IN LISTS undefined_lines)
        string(STRIP "${line}" symbol)
        if(symbol MATCHES "^U[ \t]+(.+)$")
            set(symbol "${CMAKE_MATCH_1}")
        endif()
        if(symbol MATCHES "^_(.+)$")
            set(symbol "${CMAKE_MATCH_1}")
        endif()
        if(symbol MATCHES "^clover_")
            list(FIND allowed_symbols "${symbol}" symbol_idx)
            if(symbol_idx EQUAL -1)
                list(APPEND disallowed_symbols "${symbol}")
            endif()
        endif()
    endforeach()

    if(disallowed_symbols)
        list(REMOVE_DUPLICATES disallowed_symbols)
        list(JOIN disallowed_symbols "\n  " disallowed_text)
        message(FATAL_ERROR
                "Native module ${MODULE_PATH} references clover_* symbols "
                "outside the extension API:\n  ${disallowed_text}")
    endif()
endif()
