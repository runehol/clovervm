# Export policy:
#
# - Embedder API symbols are the narrow public VM interface used by programs
#   that embed clovervm.
# - Extension API symbols are the reverse-lookup surface available to native
#   extension modules. Keep this list explicit; adding a public clover_* symbol
#   should force a decision about whether it belongs to embedders, extensions,
#   both, or neither.
set(CLOVERVM_EMBEDDER_API_SYMBOLS
    clover_vm_destroy
    clover_vm_new
    clover_vm_run_file
    clover_vm_run_interactive)

set(CLOVERVM_EXTENSION_MODULE_BUILDER_API_SYMBOLS
    clover_module_add_int_constant
    clover_module_add_function_0
    clover_module_add_function_1
    clover_module_add_function_2
    clover_module_add_function_3
    clover_module_add_function_4
    clover_module_add_function_5
    clover_module_add_function_6
    clover_module_add_function_7
    clover_module_add_string_constant)

set(CLOVERVM_EXTENSION_RUNTIME_API_SYMBOLS
    clover_float_as_double
    clover_float_from_double
    clover_int64
    clover_none
    clover_propagate_error
    clover_raise_value_error)

set(CLOVERVM_EXTENSION_API_SYMBOLS
    ${CLOVERVM_EXTENSION_MODULE_BUILDER_API_SYMBOLS}
    ${CLOVERVM_EXTENSION_RUNTIME_API_SYMBOLS})

function(clovervm_assert_unique_symbols list_name)
    set(symbols ${${list_name}})
    set(unique_symbols ${symbols})
    list(REMOVE_DUPLICATES unique_symbols)
    list(LENGTH symbols symbol_count)
    list(LENGTH unique_symbols unique_symbol_count)
    if(NOT symbol_count EQUAL unique_symbol_count)
        message(FATAL_ERROR "${list_name} contains duplicate symbols")
    endif()
endfunction()

clovervm_assert_unique_symbols(CLOVERVM_EMBEDDER_API_SYMBOLS)
clovervm_assert_unique_symbols(CLOVERVM_EXTENSION_API_SYMBOLS)

function(clovervm_write_symbol_list output_path)
    cmake_parse_arguments(ARG "EMBEDDER_API;EXTENSION_API" "" "" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "Unexpected arguments to clovervm_write_symbol_list: "
                "${ARG_UNPARSED_ARGUMENTS}")
    endif()

    set(symbols)
    if(ARG_EMBEDDER_API)
        list(APPEND symbols ${CLOVERVM_EMBEDDER_API_SYMBOLS})
    endif()
    if(ARG_EXTENSION_API)
        list(APPEND symbols ${CLOVERVM_EXTENSION_API_SYMBOLS})
    endif()
    list(REMOVE_DUPLICATES symbols)
    list(SORT symbols)

    file(WRITE "${output_path}" "")
    foreach(symbol IN LISTS symbols)
        file(APPEND "${output_path}" "${symbol}\n")
    endforeach()
endfunction()

function(clovervm_export_symbols target_name)
    cmake_parse_arguments(ARG "EMBEDDER_API;EXTENSION_API" "" "" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "Unexpected arguments to clovervm_export_symbols: "
                "${ARG_UNPARSED_ARGUMENTS}")
    endif()

    set(symbols)
    if(ARG_EMBEDDER_API)
        list(APPEND symbols ${CLOVERVM_EMBEDDER_API_SYMBOLS})
    endif()
    if(ARG_EXTENSION_API)
        list(APPEND symbols ${CLOVERVM_EXTENSION_API_SYMBOLS})
    endif()
    list(REMOVE_DUPLICATES symbols)

    if(NOT symbols)
        return()
    endif()

    if(APPLE)
        set(export_options)
        foreach(symbol IN LISTS symbols)
            list(APPEND export_options "LINKER:-exported_symbol,_${symbol}")
        endforeach()
        target_link_options(${target_name} PRIVATE ${export_options})
    elseif(UNIX)
        get_target_property(target_type ${target_name} TYPE)
        set(export_file "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.exports")

        file(WRITE "${export_file}" "{\n")
        foreach(symbol IN LISTS symbols)
            file(APPEND "${export_file}" "    ${symbol};\n")
        endforeach()
        file(APPEND "${export_file}" "};\n")

        if(target_type STREQUAL "EXECUTABLE")
            target_link_options(${target_name} PRIVATE
                LINKER:--dynamic-list,${export_file})
        else()
            file(WRITE "${export_file}" "{\n    global:\n")
            foreach(symbol IN LISTS symbols)
                file(APPEND "${export_file}" "        ${symbol};\n")
            endforeach()
            file(APPEND "${export_file}" "    local:\n        *;\n};\n")
            target_link_options(${target_name} PRIVATE
                LINKER:--version-script,${export_file})
        endif()
    endif()
endfunction()
