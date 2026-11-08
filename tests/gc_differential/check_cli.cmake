foreach(required IN ITEMS RUNNER FIXTURE_DIR TEST_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${TEST_DIR}")

function(expect_heap name expected)
    execute_process(
        COMMAND "${RUNNER}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 15)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: failed (${result}): ${error}")
    endif()
    if(NOT "${error}" STREQUAL "")
        message(FATAL_ERROR "${name}: unexpected stderr: ${error}")
    endif()
    file(READ "${expected}" expected_json)
    # JSON member order and whitespace are irrelevant; array order is significant.
    string(JSON equal ERROR_VARIABLE json_error EQUAL "${output}" "${expected_json}")
    if(NOT "${json_error}" STREQUAL "NOTFOUND" OR NOT equal)
        message(FATAL_ERROR "${name}: heap differs from ${expected}: ${json_error}\n${output}")
    endif()
endfunction()

function(expect_failure name diagnostic)
    execute_process(
        COMMAND "${RUNNER}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 15)
    # Reject crashes/timeouts too: malformed input must fail through the CLI's
    # normal error path, with no partial successful-looking heap on stdout.
    if(NOT "${result}" STREQUAL "1")
        message(FATAL_ERROR "${name}: expected exit 1, got ${result}: ${error}")
    endif()
    if(NOT "${output}" STREQUAL "")
        message(FATAL_ERROR "${name}: emitted stdout on failure: ${output}")
    endif()
    string(FIND "${error}" "gc_cpp:" prefix)
    string(FIND "${error}" "${diagnostic}" detail)
    if(NOT prefix EQUAL 0 OR detail LESS 0)
        message(FATAL_ERROR "${name}: missing diagnostic '${diagnostic}': ${error}")
    endif()
endfunction()

set(before "${FIXTURE_DIR}/before.json")
expect_heap(mark "${FIXTURE_DIR}/after_mark.json"
    --algorithm mark --collection full "${before}")
expect_heap(evacuate "${FIXTURE_DIR}/after.json"
    --algorithm evacuate --collection full "${before}")
expect_heap(young "${FIXTURE_DIR}/young_after.json"
    --algorithm evacuate --collection young "${FIXTURE_DIR}/young_before.json")
expect_heap(full-with-remembered "${FIXTURE_DIR}/young_full_after.json"
    --algorithm evacuate --collection full "${FIXTURE_DIR}/young_before.json")
expect_heap(young-weak "${FIXTURE_DIR}/weak_young_after.json"
    --algorithm evacuate --collection young "${before}")
expect_heap(remembered-weak "${FIXTURE_DIR}/remembered_weak_after.json"
    --algorithm evacuate --collection young "${FIXTURE_DIR}/remembered_weak_before.json")
expect_heap(full-remembered-weak "${FIXTURE_DIR}/remembered_weak_full_after.json"
    --algorithm evacuate --collection full "${FIXTURE_DIR}/remembered_weak_before.json")

# Exercise exact paths and end-of-options handling without a shell.
configure_file("${before}" "${TEST_DIR}/input heap, with spaces.json" COPYONLY)
expect_heap(path-with-spaces-and-comma "${FIXTURE_DIR}/after.json"
    --collection full --algorithm=evacuate -- "${TEST_DIR}/input heap, with spaces.json")

file(WRITE "${TEST_DIR}/empty.json" [=[
{"format":"clover-gc-heap","version":2,"roots":[],"objects":[],"external_weak":[],"remembered":[]}
]=])
foreach(algorithm IN ITEMS mark evacuate)
    expect_heap("empty-${algorithm}" "${TEST_DIR}/empty.json"
        --algorithm "${algorithm}" --collection full "${TEST_DIR}/empty.json")
endforeach()

execute_process(COMMAND "${RUNNER}" --help
    RESULT_VARIABLE help_result OUTPUT_VARIABLE help_output ERROR_VARIABLE help_error
    TIMEOUT 15)
string(FIND "${help_output}" "--algorithm" help_algorithm)
string(FIND "${help_output}" "FILE" help_file)
if(NOT "${help_result}" STREQUAL "0" OR NOT "${help_error}" STREQUAL ""
   OR help_algorithm LESS 0 OR help_file LESS 0)
    message(FATAL_ERROR "help: invalid help output (${help_result}): ${help_output}${help_error}")
endif()

expect_failure(no-arguments "--algorithm")
expect_failure(missing-algorithm "--algorithm" --collection full "${before}")
expect_failure(missing-collection "--collection" --algorithm mark "${before}")
expect_failure(missing-file "one input heap file" --algorithm mark --collection full)
expect_failure(extra-file "one input heap file" --algorithm mark --collection full "${before}" "${before}")
expect_failure(young "evacuation only" --algorithm mark --collection young "${before}")
expect_failure(unknown-collection "unsupported collection" --algorithm mark --collection unknown "${before}")
expect_failure(unknown-algorithm "unsupported algorithm" --algorithm unknown --collection full "${before}")
expect_failure(unknown-option "unknown-option" --algorithm mark --collection full --unknown-option "${before}")
expect_failure(missing-option-value "gc_cpp:" --algorithm)
expect_failure(duplicate-algorithm "exactly one --algorithm"
    --algorithm mark --algorithm mark --collection full "${before}")
expect_failure(conflicting-algorithm "exactly one --algorithm"
    --algorithm mark --algorithm evacuate --collection full "${before}")
expect_failure(duplicate-collection "exactly one --collection"
    --algorithm mark --collection full --collection full "${before}")
expect_failure(conflicting-collection "exactly one --collection"
    --algorithm mark --collection full --collection young "${before}")
expect_failure(unreadable-file "cannot open input file"
    --algorithm mark --collection full "${TEST_DIR}/does-not-exist.json")
expect_failure(unreadable-directory "cannot read input file"
    --algorithm mark --collection full "${TEST_DIR}")
file(WRITE "${TEST_DIR}/malformed.json" "{broken")
expect_failure(malformed-json "$" --algorithm mark --collection full "${TEST_DIR}/malformed.json")
file(WRITE "${TEST_DIR}/invalid-reference.json" [=[
{"format":"clover-gc-heap","version":2,"roots":[{"ref":0}],"objects":[],"external_weak":[],"remembered":[]}
]=])
expect_failure(invalid-reference "reference names no allocation"
    --algorithm evacuate --collection full "${TEST_DIR}/invalid-reference.json")
foreach(algorithm IN ITEMS mark evacuate)
    expect_failure("completed-${algorithm}" "fresh heap"
        --algorithm "${algorithm}" --collection full "${FIXTURE_DIR}/after.json")
endforeach()
message(STATUS "GC CLI checks passed")
